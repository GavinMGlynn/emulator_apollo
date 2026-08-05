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
  /* "It will then enter the idle state" -- §4.3, and that is the whole phase,
   * not just the status bits. This cleared the register and left the phase
   * where it was, which did not show while a SELECT only set `BSY`: the status
   * was reset and the phase had never moved. Now that a SELECT enters the
   * command state, a RESET that left it there would have the controller
   * accepting command bytes it had just been told to forget.
   *
   * Caught by `omti_suite`'s byte-for-byte comparison of a reset controller
   * against a fresh one -- the strongest form of that assertion, and the reason
   * it is written that way rather than field by field. */
  omti->phase = AP_OMTI_PHASE_IDLE;
  omti->command_index = 0u;
  omti->command_length = 0u;
  omti->buffer_index = 0u;
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

uint8_t ap_omti_last_command(const ap_omti_t *omti) {
  return omti->last_command;
}

unsigned ap_omti_command_count(const ap_omti_t *omti) {
  return omti->command_count;
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
  /* §4.3's status state: "The controller sets the C/D bit and the I/O bit in
   * the STATUS byte", the byte waiting is a status byte and it travels *to* the
   * host -- and `IREQ` is "1 = Command Complete".
   *
   * **`REQ` is set too, and the manual's sentence about it is ambiguous.** It
   * reads "If the INTERRUPT ENABLE bit was previously set in the MASK register,
   * the REQ bit is set in the STATUS byte, along with IRQ14 on the system bus",
   * which taken literally would leave a polled driver with no request to wait
   * on and no way to collect the status byte at all -- and §4.2's MASK entry
   * describes programmed I/O as a supported mode, not an unsupported one. The
   * reading taken is that `REQ` is the status state's own handshake and the
   * *interrupt* is what the enable bit gates. Recorded because it is a reading
   * rather than a quotation. */
  omti->status |= (uint8_t)(AP_OMTI_ST_IREQ | AP_OMTI_ST_CD |
                            AP_OMTI_ST_IO | AP_OMTI_ST_REQ);
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
  omti->transfer_length = AP_AWD_SECTOR_BYTES;
  omti->phase = AP_OMTI_PHASE_DATA_IN;
  /* Data rather than a command or status byte, travelling *to* the host, and
   * requested.
   *
   * `DREQ` is **not** unconditional, which it was: §4.3 gates it on the MASK's
   * DMA ENABLE -- "If the DMA ENABLE bit in the MASK byte has been previously
   * set, data will be transferred in DMA mode ... it will set the DREQ bit". A
   * controller asserting it in programmed I/O asks for a DMA cycle nobody
   * arranged. */
  omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_IO |
                           AP_OMTI_ST_REQ);
  if ((omti->mask & AP_OMTI_MASK_DMA_ENABLE) != 0u) {
    omti->status |= AP_OMTI_ST_DREQ;
  }
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
  omti->last_command = cdb.command;
  omti->command_count++;

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
    omti->transfer_length = (unsigned)sizeof omti->sense;
    omti->blocks_left = 0u;
    omti->phase = AP_OMTI_PHASE_DATA_IN;
    /* Requested, and travelling to the host. `DREQ` only in DMA mode -- this
     * asserted it unconditionally, the same defect the read path had. */
    omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_IO |
                             AP_OMTI_ST_REQ);
    if ((omti->mask & AP_OMTI_MASK_DMA_ENABLE) != 0u) {
      omti->status |= AP_OMTI_ST_DREQ;
    }
    return;

  case AP_OMTI_CMD_TEST_DRIVE_READY:
    finish(omti, omti->drive == NULL, SENSE_DRIVE_NOT_READY);
    return;

  case AP_OMTI_CMD_READ_CONFIGURATION: {
    /* §5.4.29, ten bytes describing the drive. ESDI only, which
     * `ap_omti_cdb_accepted_by_esdi` has already checked. */
    if (omti->drive == NULL) {
      finish(omti, true, SENSE_DRIVE_NOT_READY);
      return;
    }
    const ap_awd_geometry_t g = omti->drive->geometry;
    /* **One less than the count**, as the manual marks them. A model returning
     * the counts describes a drive one cylinder, one head and one sector larger
     * than it has. */
    const uint16_t highest_cylinder = (uint16_t)(g.cylinders - 1u);
    memset(omti->buffer, 0, AP_OMTI_CONFIGURATION_BYTES);
    omti->buffer[0] = (uint8_t)(highest_cylinder >> 8);
    omti->buffer[1] = (uint8_t)highest_cylinder;
    omti->buffer[2] = (uint8_t)(g.heads - 1u);
    omti->buffer[3] = (uint8_t)(g.sectors - 1u);
    /* Bytes 4-9 stay zero: physical formatting parameters this project has no
     * source for. See the header. */
    omti->buffer_index = 0u;
    omti->transfer_length = AP_OMTI_CONFIGURATION_BYTES;
    omti->blocks_left = 0u;
    omti->phase = AP_OMTI_PHASE_DATA_IN;
    omti->status = (uint8_t)((omti->status & ~AP_OMTI_ST_CD) | AP_OMTI_ST_IO |
                             AP_OMTI_ST_REQ);
    if ((omti->mask & AP_OMTI_MASK_DMA_ENABLE) != 0u) {
      omti->status |= AP_OMTI_ST_DREQ;
    }
    return;
  }

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
  /* "When the command byte is written, the controller de-asserts the REQ bit
   * and moves the command byte into its buffer." The write *is* the
   * acknowledgement, so `REQ` is cleared here and re-asserted below only if
   * another byte is wanted. */
  omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_REQ);

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
    if (omti->command_index == 0u) {
      /* The first byte after a SELECT. Its own opcode says how long the block
       * is -- §5.1.1 -- so the length is not known until it arrives. */
      omti->command_length = ap_omti_cdb_length(value);
    }
    if (omti->command_index < sizeof omti->command) {
      omti->command[omti->command_index++] = value;
    }
    if (omti->command_index >= omti->command_length) {
      /* "C/D is then de-asserted and the data state is entered." */
      omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_CD);
      execute(omti);
      return;
    }
    /* "This handshaking is repeated until all command bytes are transferred." */
    omti->status |= AP_OMTI_ST_REQ;
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
    /* The read is the acknowledgement of the request that offered this byte. */
    omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_REQ);
    if (omti->buffer_index < omti->transfer_length) {
      omti->status |= AP_OMTI_ST_REQ;
      return value;
    }
    /* The transfer is done. A sector-carrying command may have more sectors to
     * send; anything else ends here. */
    if (omti->blocks_left > 0u) {
      feed(omti);
    } else {
      finish(omti, false, 0u);
    }
    return value;
  }
  case AP_OMTI_PHASE_STATUS: {
    const uint8_t value = omti->completion;
    /* §4.3, and all of it: "When the STATUS byte is read from the DATA IN
     * register, the controller clears the IREQ and IRQ14 (if enabled), clears
     * C/D, I/O, and BSY bits in the STATUS Registers, and enters the idle
     * state."
     *
     * This cleared `IREQ` and `DREQ` and left `C/D`, `I/O`, `BSY` and `REQ`
     * standing -- so a driver that had just collected a completion still saw a
     * **selected controller asking for another byte**, which is a machine that
     * never finishes a command however many it runs. `BSY` is the one that
     * matters most: "0 = Controller is Idle" is how a driver knows it may start
     * the next command at all.
     *
     * The read is also the acknowledgement of the request that offered this
     * byte, so `REQ` clears with the rest. What is left is `C0`, the two fixed
     * bits -- which is exactly the measured idle controller `FINDINGS.md` C21
     * recorded. */
    omti->phase = AP_OMTI_PHASE_IDLE;
    omti->status = (uint8_t)(omti->status &
                             ~(AP_OMTI_ST_IREQ | AP_OMTI_ST_DREQ |
                               AP_OMTI_ST_CD | AP_OMTI_ST_IO |
                               AP_OMTI_ST_BSY | AP_OMTI_ST_REQ));
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
    /* "SELECT (Function)", and §4.3 spells out what follows it.
     *
     * "During the SELECTION STATE, the controller responds to a selection
     * request by asserting the BSY bit ... The controller then enters the
     * command state ... First, the C/D bit of the STATUS register is set. Then
     * the REQ bit is set, asking for the first command byte to be written to
     * the DATA OUT register in BYTE mode."
     *
     * All three, in that order and in one step: a model asserting only `BSY`
     * leaves the host waiting for a request that never comes. `I/O` stays clear
     * because the transfer is *to* the controller.
     *
     * "The IDLE STATE is the only time the controller will respond to a select
     * request", so a select while busy is ignored rather than restarting the
     * sequence -- which would let a driver's stray write discard a command it
     * had half sent. */
    if (omti->phase != AP_OMTI_PHASE_IDLE) {
      return;
    }
    omti->status |= AP_OMTI_ST_BSY | AP_OMTI_ST_CD | AP_OMTI_ST_REQ;
    omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_IO);
    omti->phase = AP_OMTI_PHASE_COMMAND;
    omti->command_index = 0u;
    omti->command_length = 0u;
    return;
  case AP_OMTI_DISK_MASK:
    omti->mask = value;
    return;
  }
}

/* ---- The floppy half's command phase, `[OMTI]` §6.3 ------------------------ */

void ap_omti_attach_floppy(ap_omti_t *omti, ap_afd_t *floppy) {
  omti->floppy = floppy;
}

ap_omti_phase_t ap_omti_fdc_phase(const ap_omti_t *omti) {
  return omti->fdc_phase;
}

/* §6.3's command and result lengths, counting the opcode byte in the first.
 *
 * READ DATA and the three scans take the same nine: opcode, HD/US, C, H, R, N,
 * EOT, GPL, and a last byte that is DTL on the read and STP on the scans.
 * FORMAT A TRACK takes six, and every other command two or one. */
unsigned ap_omti_fdc_command_bytes(uint8_t opcode) {
  switch (opcode & AP_OMTI_FDC_OPCODE_MASK) {
  case AP_OMTI_FDC_READ_DATA:
  case AP_OMTI_FDC_SCAN_EQUAL:
  case AP_OMTI_FDC_SCAN_LOW_EQUAL:
  case AP_OMTI_FDC_SCAN_HIGH_EQUAL:
    return 9u;
  case AP_OMTI_FDC_FORMAT_TRACK:
    return 6u; /* opcode, HD/US, N, SC, GPL, D */
  case AP_OMTI_FDC_SPECIFY:
    return 3u; /* opcode, SRT/HUT, HLT/ND */
  case AP_OMTI_FDC_SEEK:
    return 3u; /* opcode, HD/US, NCN */
  case AP_OMTI_FDC_SENSE_DRIVE:
  case AP_OMTI_FDC_RECALIBRATE:
    return 2u; /* opcode, HD/US */
  case AP_OMTI_FDC_SENSE_INTERRUPT:
    return 1u; /* opcode alone */
  default:
    /* §6.3.11's INVALID. The manual gives it "HD/US" as a further byte, but a
     * controller cannot know the length of a command it does not recognise: it
     * has only the one byte it was handed. So the command ends at the opcode
     * and the result phase carries the ST0 that says so. */
    return 1u;
  }
}

unsigned ap_omti_fdc_result_bytes(uint8_t opcode) {
  switch (opcode & AP_OMTI_FDC_OPCODE_MASK) {
  case AP_OMTI_FDC_READ_DATA:
  case AP_OMTI_FDC_FORMAT_TRACK:
  case AP_OMTI_FDC_SCAN_EQUAL:
  case AP_OMTI_FDC_SCAN_LOW_EQUAL:
  case AP_OMTI_FDC_SCAN_HIGH_EQUAL:
    return 7u; /* ST0 ST1 ST2 C H R N */
  case AP_OMTI_FDC_SENSE_INTERRUPT:
    return 2u; /* ST0, PCN */
  case AP_OMTI_FDC_SENSE_DRIVE:
    return 1u; /* ST3 */
  case AP_OMTI_FDC_SPECIFY:
  case AP_OMTI_FDC_SEEK:
  case AP_OMTI_FDC_RECALIBRATE:
    /* "none". A driver polls SENSE INTERRUPT STATUS for the two that move the
     * head, and SPECIFY reports nothing at all. */
    return 0u;
  default:
    return 1u; /* INVALID's ST0 */
  }
}

/* Which drive the second command byte selects, and where its head is. */
static unsigned fdc_unit(const ap_omti_t *omti) {
  if (omti->fdc_command_length < 2u) {
    return omti->dor & AP_OMTI_DOR_SELECT_B ? 1u : 0u;
  }
  return omti->fdc_command[1] & 1u;
}

/* Put the controller back where a driver expects to write the next command
 * byte: ready, expecting input, not busy. */
static void fdc_idle(ap_omti_t *omti) {
  omti->fdc_phase = AP_OMTI_PHASE_IDLE;
  omti->fdc_command_length = 0u;
  omti->fdc_command_index = 0u;
  omti->fdc_result_length = 0u;
  omti->fdc_result_index = 0u;
  omti->fdc_buffer_index = 0u;
  omti->fdc_buffer_length = 0u;
  omti->fdc_status = AP_OMTI_MSR_RQM;
}

/* Enter the result phase, or go straight back to idle when §6.3 gives the
 * command no result bytes. */
static void fdc_result(ap_omti_t *omti) {
  if (omti->fdc_result_length == 0u) {
    fdc_idle(omti);
    return;
  }
  omti->fdc_phase = AP_OMTI_PHASE_STATUS;
  omti->fdc_result_index = 0u;
  /* Controller to host, and busy until the last byte is taken. */
  omti->fdc_status = AP_OMTI_MSR_RQM | AP_OMTI_MSR_DIO | AP_OMTI_MSR_BUSY;
}

/* The seven-byte result the data commands share. C, H, R and N come back as the
 * position *after* the operation, which is what a driver chains from. */
static void fdc_data_result(ap_omti_t *omti, uint8_t st0, uint8_t st1,
                            uint8_t st2) {
  omti->fdc_result[0] = (uint8_t)(st0 | (uint8_t)fdc_unit(omti));
  omti->fdc_result[1] = st1;
  omti->fdc_result[2] = st2;
  omti->fdc_result[3] = omti->fdc_command[2]; /* C */
  omti->fdc_result[4] = omti->fdc_command[3]; /* H */
  omti->fdc_result[5] = omti->fdc_command[4]; /* R */
  omti->fdc_result[6] = omti->fdc_command[5]; /* N */
  omti->fdc_result_length = 7u;
}

/* Read the sector the command's C/H/R name into the buffer. */
static bool fdc_load_sector(ap_omti_t *omti, uint8_t *st1) {
  uint32_t lba = 0u;
  *st1 = 0u;
  if (omti->floppy == nullptr) {
    /* An empty drive: the sector is not there to be found. */
    *st1 = AP_OMTI_ST1_NO_DATA | AP_OMTI_ST1_MISSING_MARK;
    return false;
  }
  if (!ap_afd_lba(omti->fdc_command[2], omti->fdc_command[3],
                  omti->fdc_command[4], &lba) ||
      !ap_afd_read(omti->floppy, lba, omti->fdc_buffer)) {
    *st1 = AP_OMTI_ST1_NO_DATA;
    return false;
  }
  omti->fdc_buffer_index = 0u;
  omti->fdc_buffer_length = AP_AFD_SECTOR_BYTES;
  return true;
}

/* Run the command now that every byte of it has arrived. */
static void fdc_execute(ap_omti_t *omti) {
  const uint8_t opcode =
      (uint8_t)(omti->fdc_command[0] & AP_OMTI_FDC_OPCODE_MASK);
  const unsigned unit = fdc_unit(omti);
  uint8_t st1 = 0u;

  omti->fdc_result_length = ap_omti_fdc_result_bytes(opcode);

  switch (opcode) {
  case AP_OMTI_FDC_READ_DATA:
    if (!fdc_load_sector(omti, &st1)) {
      fdc_data_result(omti, AP_OMTI_ST0_IC_ABRUPT, st1, 0u);
      fdc_result(omti);
      return;
    }
    /* The data phase: the host reads the sector a byte at a time from the data
     * register, and the result phase follows the last of them. */
    omti->fdc_phase = AP_OMTI_PHASE_DATA_IN;
    omti->fdc_status = AP_OMTI_MSR_RQM | AP_OMTI_MSR_DIO | AP_OMTI_MSR_BUSY;
    return;

  case AP_OMTI_FDC_FORMAT_TRACK:
    /* §6.3.2 takes N, SC, GPL and a fill byte D, and writes a whole track of
     * sectors carrying it. With no media, or on a read-only image, it is the
     * write-protect bit that says so -- ST1's Not Writeable is documented for
     * exactly this command. */
    if (omti->floppy == nullptr || !omti->floppy->writable) {
      fdc_data_result(omti, AP_OMTI_ST0_IC_ABRUPT,
                      AP_OMTI_ST1_NOT_WRITEABLE, 0u);
      fdc_result(omti);
      return;
    }
    {
      const uint8_t fill = omti->fdc_command[5];
      const uint8_t sectors = omti->fdc_command[3]; /* SC */
      const uint8_t cylinder = omti->fdc_cylinder[unit];
      const uint8_t head = (uint8_t)((omti->fdc_command[1] >> 2) & 1u);
      memset(omti->fdc_buffer, fill, AP_AFD_SECTOR_BYTES);
      for (uint8_t sector = 1u; sector <= sectors; ++sector) {
        uint32_t lba = 0u;
        if (!ap_afd_lba(cylinder, head, sector, &lba) ||
            !ap_afd_write(omti->floppy, lba, omti->fdc_buffer)) {
          fdc_data_result(omti, AP_OMTI_ST0_IC_ABRUPT, AP_OMTI_ST1_NO_DATA, 0u);
          fdc_result(omti);
          return;
        }
      }
      /* C, H, R and N are read back from the command bytes as ever; FORMAT's
       * command has no C or H, so the two positions carry what it was handed. */
      fdc_data_result(omti, AP_OMTI_ST0_IC_NORMAL, 0u, 0u);
      omti->fdc_result[3] = cylinder;
      omti->fdc_result[4] = head;
      fdc_result(omti);
      return;
    }

  case AP_OMTI_FDC_SCAN_EQUAL:
  case AP_OMTI_FDC_SCAN_LOW_EQUAL:
  case AP_OMTI_FDC_SCAN_HIGH_EQUAL:
    /* §6.3.3-5 compare the sector against bytes the host sends, so the data
     * phase runs the other way: the controller takes the comparison data and
     * ST2 reports the verdict. */
    if (!fdc_load_sector(omti, &st1)) {
      fdc_data_result(omti, AP_OMTI_ST0_IC_ABRUPT, st1, 0u);
      fdc_result(omti);
      return;
    }
    omti->fdc_phase = AP_OMTI_PHASE_DATA_OUT;
    omti->fdc_status = AP_OMTI_MSR_RQM | AP_OMTI_MSR_BUSY;
    /* Assume the hit until a byte contradicts it: a scan of zero bytes is
     * satisfied, and each byte can only take the flag away. */
    omti->fdc_result[2] = AP_OMTI_ST2_SCAN_HIT;
    return;

  case AP_OMTI_FDC_RECALIBRATE:
    /* §6.3.6 steps to track 0. Equipment Check is what a drive that never gets
     * there reports, and this one always does. */
    omti->fdc_cylinder[unit] = 0u;
    omti->fdc_seek_done = true;
    omti->fdc_seek_st0 =
        (uint8_t)(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END | (uint8_t)unit);
    fdc_result(omti);
    return;

  case AP_OMTI_FDC_SEEK:
    /* §6.3.10's NCN, which is simply where the head now is. */
    omti->fdc_cylinder[unit] = omti->fdc_command[2];
    omti->fdc_seek_done = true;
    omti->fdc_seek_st0 =
        (uint8_t)(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END | (uint8_t)unit);
    fdc_result(omti);
    return;

  case AP_OMTI_FDC_SENSE_INTERRUPT:
    /* §6.3.7 returns ST0 and the present cylinder. Issued with no seek
     * outstanding it is the invalid case -- that is how a driver ends the
     * polling loop after a reset, rather than by counting. */
    if (omti->fdc_seek_done) {
      omti->fdc_result[0] = omti->fdc_seek_st0;
      omti->fdc_result[1] =
          omti->fdc_cylinder[omti->fdc_seek_st0 & AP_OMTI_ST0_UNIT_MASK];
      omti->fdc_seek_done = false;
    } else {
      omti->fdc_result[0] = AP_OMTI_ST0_IC_INVALID;
      omti->fdc_result[1] = 0u;
    }
    fdc_result(omti);
    return;

  case AP_OMTI_FDC_SENSE_DRIVE:
    /* §6.3.9's ST3. Bit 0 is "always 1"; track 0 and the head come from where
     * the drive actually is, and write protect from the image. */
    omti->fdc_result[0] = AP_OMTI_ST3_ALWAYS | (uint8_t)unit;
    if (omti->fdc_cylinder[unit] == 0u) {
      omti->fdc_result[0] |= AP_OMTI_ST3_TRACK_0;
    }
    if ((omti->fdc_command[1] & 0x04u) != 0u) {
      omti->fdc_result[0] |= AP_OMTI_ST3_HEAD;
    }
    if (omti->floppy == nullptr || !omti->floppy->writable) {
      omti->fdc_result[0] |= AP_OMTI_ST3_WRITE_PROTECT;
    }
    fdc_result(omti);
    return;

  case AP_OMTI_FDC_SPECIFY:
    /* §6.3.8's step rate, head load and head unload times. They pace a real
     * drive's mechanics; nothing in this core is timed off them yet, so the
     * bytes are accepted and kept and the command has no result phase. */
    fdc_result(omti);
    return;

  default:
    /* §6.3.11. "Invalid Command Issue (IC) - The issued command was never
     * started", which is ST0's `10` and the only byte that comes back. */
    omti->fdc_result[0] = AP_OMTI_ST0_IC_INVALID;
    fdc_result(omti);
    return;
  }
}

/* A byte arriving at the data register. */
static void fdc_take_byte(ap_omti_t *omti, uint8_t value) {
  if (omti->fdc_phase == AP_OMTI_PHASE_DATA_OUT) {
    /* A scan's comparison byte. Each one can only clear the hit. */
    const uint8_t opcode =
        (uint8_t)(omti->fdc_command[0] & AP_OMTI_FDC_OPCODE_MASK);
    const uint8_t media = omti->fdc_buffer[omti->fdc_buffer_index];
    bool matched = false;
    switch (opcode) {
    case AP_OMTI_FDC_SCAN_LOW_EQUAL:
      matched = media <= value;
      break;
    case AP_OMTI_FDC_SCAN_HIGH_EQUAL:
      matched = media >= value;
      break;
    default:
      matched = media == value;
      break;
    }
    if (!matched) {
      omti->fdc_result[2] = AP_OMTI_ST2_SCAN_NOT_SATISFIED;
    }
    ++omti->fdc_buffer_index;
    if (omti->fdc_buffer_index >= omti->fdc_buffer_length) {
      const uint8_t st2 = omti->fdc_result[2];
      fdc_data_result(omti, AP_OMTI_ST0_IC_NORMAL, 0u, st2);
      fdc_result(omti);
    }
    return;
  }

  if (omti->fdc_phase == AP_OMTI_PHASE_IDLE) {
    omti->fdc_phase = AP_OMTI_PHASE_COMMAND;
    omti->fdc_command_index = 0u;
    omti->fdc_command_length = ap_omti_fdc_command_bytes(value);
  }
  if (omti->fdc_phase != AP_OMTI_PHASE_COMMAND) {
    return;
  }
  if (omti->fdc_command_index < AP_OMTI_FDC_COMMAND_MAX) {
    omti->fdc_command[omti->fdc_command_index] = value;
  }
  ++omti->fdc_command_index;
  if (omti->fdc_command_index >= omti->fdc_command_length) {
    omti->fdc_command_length = omti->fdc_command_index;
    fdc_execute(omti);
    return;
  }
  omti->fdc_status = AP_OMTI_MSR_RQM | AP_OMTI_MSR_BUSY;
}

/* A byte leaving the data register. */
static uint8_t fdc_give_byte(ap_omti_t *omti) {
  if (omti->fdc_phase == AP_OMTI_PHASE_DATA_IN) {
    const uint8_t value = omti->fdc_buffer[omti->fdc_buffer_index];
    ++omti->fdc_buffer_index;
    if (omti->fdc_buffer_index >= omti->fdc_buffer_length) {
      fdc_data_result(omti, AP_OMTI_ST0_IC_NORMAL, 0u, 0u);
      fdc_result(omti);
    }
    return value;
  }
  if (omti->fdc_phase == AP_OMTI_PHASE_STATUS) {
    const uint8_t value = omti->fdc_result[omti->fdc_result_index];
    ++omti->fdc_result_index;
    if (omti->fdc_result_index >= omti->fdc_result_length) {
      fdc_idle(omti);
    }
    return value;
  }
  /* Nothing to give: the last byte written stands, which is what a register
   * with no driver behind it does. */
  return omti->fdc_data;
}

uint8_t ap_omti_fdc_read(ap_omti_t *omti, unsigned reg) {
  switch (reg & (AP_OMTI_FLOPPY_REGISTERS - 1u)) {
  case AP_OMTI_FDC_MSR:
    return omti->fdc_status;
  case AP_OMTI_FDC_DATA:
    if (ap_omti_fdc_in_reset(omti)) {
      return omti->fdc_data;
    }
    omti->fdc_data = fdc_give_byte(omti);
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
  case AP_OMTI_FDC_DOR: {
    const bool was_reset = ap_omti_fdc_in_reset(omti);
    omti->dor = value;
    /* Bit 2 rising takes the floppy side out of reset. Coming out is what
     * arms the command phase: before it, the data register is inert. */
    if (was_reset && !ap_omti_fdc_in_reset(omti)) {
      fdc_idle(omti);
    } else if (!was_reset && ap_omti_fdc_in_reset(omti)) {
      omti->fdc_phase = AP_OMTI_PHASE_IDLE;
      omti->fdc_status = 0u;
    }
    return;
  }
  case AP_OMTI_FDC_DATA:
    omti->fdc_data = value;
    if (!ap_omti_fdc_in_reset(omti)) {
      fdc_take_byte(omti, value);
    }
    return;
  case AP_OMTI_FDC_CONTROL:
  case AP_OMTI_FDC_DIR:
    omti->fdc_control = value;
    return;
  default:
    return;
  }
}
