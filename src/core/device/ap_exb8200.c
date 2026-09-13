#include "device/ap_exb8200.h"

#include <string.h>

/* ---- Sense, `[EXB]` §15.2 ------------------------------------------------ */

/* Build the 26 bytes. Every Check Condition goes through here, so no path can
 * report one without saying what it was. */
static void sense_set(ap_exb8200_t *drive, uint8_t key, uint8_t flags,
                      bool valid, int32_t information, uint8_t asc,
                      uint8_t ascq) {
  memset(drive->sense, 0, sizeof drive->sense);
  drive->sense[0] = (uint8_t)(AP_EXB8200_SENSE_CLASS_7 |
                              (valid ? AP_EXB8200_SENSE_VALID : 0u));
  drive->sense[2] = (uint8_t)(flags | (key & AP_EXB8200_SENSE_KEY_MASK));
  /* §15.2: "negative values (2s complement notation) are reported", and the
   * Information bytes are four, MSB first. */
  const uint32_t value = (uint32_t)information;
  drive->sense[3] = (uint8_t)(value >> 24);
  drive->sense[4] = (uint8_t)(value >> 16);
  drive->sense[5] = (uint8_t)(value >> 8);
  drive->sense[6] = (uint8_t)value;
  drive->sense[7] = AP_EXB8200_ADDITIONAL_SENSE_LENGTH;
  drive->sense[12] = asc;
  drive->sense[13] = ascq;
  drive->sense[16] = (uint8_t)(drive->error_counter >> 16);
  drive->sense[17] = (uint8_t)(drive->error_counter >> 8);
  drive->sense[18] = (uint8_t)drive->error_counter;

  /* Bytes 19-21, Table 15-3. The bits that are a *state* rather than an event
   * are computed here so they cannot be forgotten by a caller. */
  uint8_t u19 = 0u;
  if (!drive->present) {
    u19 |= AP_EXB8200_U19_TNP;
  }
  if (drive->loaded && drive->where == AP_EXB8200_AT_LBOT) {
    u19 |= AP_EXB8200_U19_LBOT;
  }
  uint8_t u20 = 0u;
  if (drive->present && !drive->media.writable) {
    u20 |= AP_EXB8200_U20_WP;
  }
  uint8_t u21 = 0u;
  if (drive->where == AP_EXB8200_AT_PEOT) {
    u21 |= AP_EXB8200_U21_PEOT;
  }
  drive->sense[19] = u19;
  drive->sense[20] = u20;
  drive->sense[21] = u21;

  /* Bytes 23-25: "the amount of tape remaining (in 1,024-byte blocks). This is
   * the LEOT position minus the current physical position." */
  const uint32_t used = ap_exb8200_physical_blocks(drive);
  const uint32_t remaining =
      drive->media.blocks_to_leot > used ? drive->media.blocks_to_leot - used
                                         : 0u;
  drive->sense[23] = (uint8_t)(remaining >> 16);
  drive->sense[24] = (uint8_t)(remaining >> 8);
  drive->sense[25] = (uint8_t)remaining;
  drive->sense_valid = true;
}

/* A Check Condition with its sense, which is the only way to raise one. */
static uint8_t fail(ap_exb8200_t *drive, ap_scsi_result_t *result, uint8_t key,
                    uint8_t flags, bool valid, int32_t information, uint8_t asc,
                    uint8_t ascq) {
  sense_set(drive, key, flags, valid, information, asc, ascq);
  drive->check_conditions++;
  result->status = AP_SCSI_STATUS_CHECK_CONDITION;
  return AP_SCSI_STATUS_CHECK_CONDITION;
}

/* ---- The medium ---------------------------------------------------------- */

uint32_t ap_exb8200_physical_blocks(const ap_exb8200_t *drive) {
  uint32_t blocks = AP_EXB8200_LBOT_BLOCKS;
  for (unsigned i = 0; i < drive->media.records; i++) {
    const ap_exb8200_record_t *record = &drive->media.record[i];
    if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
      /* §22: 270 tracks long, 60 short, and `[EXBPS]` §4.3.3 gives a track
       * eight physical blocks. */
      blocks += (record->short_mark ? AP_EXB8200_FILEMARK_SHORT_TRACKS
                                    : AP_EXB8200_FILEMARK_LONG_TRACKS) *
                AP_EXB8200_TRACK_BLOCKS;
      continue;
    }
    /* §23.5 and §23.7: a logical block is written in 1,024-byte physical
     * blocks and the remainder of the last one is gap bytes, so a 1,536-byte
     * block costs two. */
    blocks += (record->length + AP_EXB8200_PHYSICAL_BLOCK - 1u) /
              AP_EXB8200_PHYSICAL_BLOCK;
  }
  return blocks;
}

/* Whether the tape may be written where it stands. §21.5: "valid tape positions
 * are logical beginning of tape (LBOT), blank tape, or the beginning of a long
 * filemark" -- anything else is Illegal Request. Blank tape is the end of the
 * records, and the BOT side of a long filemark is a position whose next record
 * is a long one. */
static bool writable_here(const ap_exb8200_t *drive) {
  if (drive->where == AP_EXB8200_AT_LBOT) {
    return true;
  }
  if (drive->where != AP_EXB8200_IN_DATA) {
    return false;
  }
  if (drive->at >= drive->media.records) {
    return true; /* blank tape */
  }
  const ap_exb8200_record_t *next = &drive->media.record[drive->at];
  return next->kind == AP_EXB8200_RECORD_FILEMARK && !next->short_mark;
}

/* A write at a valid position truncates everything after it: §22 says a write
 * into a long filemark's erase gap erases "the long filemark and any data
 * following the long filemark", and §21.4 that a write starts "at the next
 * logical block". */
static void truncate_here(ap_exb8200_t *drive) {
  drive->media.records = drive->at;
  uint32_t used = 0u;
  for (unsigned i = 0; i < drive->media.records; i++) {
    const ap_exb8200_record_t *record = &drive->media.record[i];
    if (record->kind == AP_EXB8200_RECORD_DATA &&
        record->offset + record->length > used) {
      used = record->offset + record->length;
    }
  }
  drive->media.data_used = used;
}

/* ---- Lifecycle ----------------------------------------------------------- */

static void defaults(ap_exb8200_t *drive) {
  /* §8.4 and §9.4's power-on values, each stated in both chapters. */
  drive->buffered = true;
  drive->block_length = AP_EXB8200_BLOCK_LENGTH_DEFAULT;
  drive->vendor_flags = 0u; /* NBE, EBD, PE and NAL all clear */
  drive->p5 = false;
  drive->motion_threshold = AP_EXB8200_MOTION_THRESHOLD_DEFAULT;
  drive->reconnect_threshold = AP_EXB8200_RECONNECT_THRESHOLD_DEFAULT;
  drive->gap_threshold = AP_EXB8200_GAP_THRESHOLD_DEFAULT;
}

void ap_exb8200_power_on(ap_exb8200_t *drive) {
  if (drive == nullptr) {
    return;
  }
  memset(drive, 0, sizeof *drive);
  defaults(drive);
  /* §23.3 and Table 15-1's Unit Attention row: the first command after a
   * power-on reset other than REQUEST SENSE or INQUIRY gets Check Condition
   * with Unit Attention, and the drive "does not perform the requested
   * command". §19's `PF` bit reports the same thing in the unit sense. */
  sense_set(drive, AP_EXB8200_KEY_UNIT_ATTENTION, 0u, false, 0,
            AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  drive->sense[19] |= AP_EXB8200_U19_PF;
  drive->unit_attention = true;
}

void ap_exb8200_reset(ap_exb8200_t *drive) {
  if (drive == nullptr) {
    return;
  }
  /* §3.4: "aborts all operations and is re-initialized to the default state".
   * §10.1 adds that a reset condition ends Prevent Medium Removal, and §16
   * that it ends a reservation. The medium stays: a reset does not eject. */
  const ap_exb8200_media_t media = drive->media;
  const bool loaded = drive->loaded;
  const bool present = drive->present;
  memset(drive, 0, sizeof *drive);
  drive->media = media;
  drive->loaded = loaded;
  drive->present = present;
  defaults(drive);
  /* A reset rewinds: §24.3's load and §17's rewind both leave LBOT, and §23.2
   * says an initialised drive sizes the cartridge, which needs a rewind. */
  drive->where = loaded ? AP_EXB8200_AT_LBOT : AP_EXB8200_IN_DATA;
  drive->at = 0u;
  sense_set(drive, AP_EXB8200_KEY_UNIT_ATTENTION, 0u, false, 0,
            AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  drive->sense[19] |= AP_EXB8200_U19_PF;
  drive->unit_attention = true;
}

bool ap_exb8200_insert(ap_exb8200_t *drive, const ap_exb8200_media_t *media) {
  if (drive == nullptr || media == nullptr || drive->present) {
    return false;
  }
  drive->media = *media;
  drive->present = true;
  /* §7: "the tape in a data cartridge is automatically loaded into the tape
   * path when the cartridge is inserted and the door is closed, unless the
   * autoload feature has been disabled", and "loading automatically positions
   * the tape at LBOT". */
  drive->loaded = (drive->vendor_flags & AP_EXB8200_VU_NAL) == 0u;
  drive->where = drive->loaded ? AP_EXB8200_AT_LBOT : AP_EXB8200_IN_DATA;
  drive->at = 0u;
  /* §4.4: "a new cartridge has been loaded" is a Unit Attention condition. */
  sense_set(drive, AP_EXB8200_KEY_UNIT_ATTENTION, 0u, false, 0,
            AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  drive->unit_attention = true;
  return true;
}

void ap_exb8200_unload_button(ap_exb8200_t *drive) {
  if (drive == nullptr || !drive->present) {
    return;
  }
  /* §23.1: pressing the button does nothing while Prevent Medium Removal is in
   * force, and nothing while data remains in the buffer. This model writes
   * through, so the second never applies. */
  if (drive->prevented) {
    return;
  }
  drive->loaded = false;
  drive->present = false;
  drive->where = AP_EXB8200_IN_DATA;
  drive->at = 0u;
  /* §20.2: "when the unload button on the front of the EXB-8200 has been used
   * to unload a data cartridge, the EXB-8200 returns Check Condition status
   * with the sense key set to **Unit Attention** and the TNP bit set" -- where
   * the UNLOAD *command* leaves Not Ready. */
  sense_set(drive, AP_EXB8200_KEY_UNIT_ATTENTION, 0u, false, 0,
            AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  drive->unit_attention = true;
}

/* ---- Command helpers ----------------------------------------------------- */

static uint32_t be24(const uint8_t *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/* §19.1: "a negative value of n (in 2s complement notation)". Three bytes. */
static int32_t signed24(const uint8_t *p) {
  const uint32_t raw = be24(p);
  return (raw & 0x800000u) != 0u ? (int32_t)(raw | 0xFF000000u)
                                 : (int32_t)raw;
}

/* Move `count` bytes out to the initiator through the ASC's own DMA. */
static unsigned give(const ap_scsi_memory_t *memory, uint32_t buffer,
                     const uint8_t *from, unsigned count, unsigned capacity) {
  if (memory == nullptr) {
    return 0u;
  }
  const unsigned moved = count < capacity ? count : capacity;
  for (unsigned i = 0; i < moved; i++) {
    memory->write(memory->context, buffer + i, from[i]);
  }
  return moved;
}

/* Whether the drive can accept a medium-access command at all. Returns the
 * status to give, or `AP_SCSI_STATUS_GOOD` when it can. §24.1 and §20.2. */
static uint8_t medium_ready(ap_exb8200_t *drive, ap_scsi_result_t *result) {
  if (!drive->present) {
    return fail(drive, result, AP_EXB8200_KEY_NOT_READY, 0u, false, 0,
                AP_EXB8200_ASC_NOT_READY, AP_EXB8200_ASCQ_NOT_MOUNTED);
  }
  if (!drive->loaded) {
    /* §24.1: "when the data cartridge has been inserted and the door closed
     * with the autoload feature disabled, or when the data cartridge has been
     * unloaded with the Prevent Medium Removal feature enabled, subsequent
     * tape motion commands result in Check Condition with Not Ready". */
    return fail(drive, result, AP_EXB8200_KEY_NOT_READY, 0u, false, 0,
                AP_EXB8200_ASC_NOT_READY, AP_EXB8200_ASCQ_NOT_MOUNTED);
  }
  return AP_SCSI_STATUS_GOOD;
}

/* ---- The commands -------------------------------------------------------- */

static uint8_t cmd_test_unit_ready(ap_exb8200_t *drive,
                                   ap_scsi_result_t *result) {
  if (!drive->present) {
    /* §20.2: Not Ready with TNP, ASC `04`, ASCQ `00`. */
    return fail(drive, result, AP_EXB8200_KEY_NOT_READY, 0u, false, 0,
                AP_EXB8200_ASC_NOT_READY, AP_EXB8200_ASCQ_NOT_MOUNTED);
  }
  if (!drive->loaded) {
    /* "present but not loaded" -- the same ASC/ASCQ without TNP, which the
     * sense builder gets right because TNP follows `present`. */
    return fail(drive, result, AP_EXB8200_KEY_NOT_READY, 0u, false, 0,
                AP_EXB8200_ASC_NOT_READY, AP_EXB8200_ASCQ_NOT_MOUNTED);
  }
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_inquiry(uint8_t lun, const uint8_t *cdb,
                           const ap_scsi_memory_t *memory, uint32_t buffer,
                           unsigned capacity, ap_scsi_result_t *result) {
  uint8_t data[AP_EXB8200_INQUIRY_BYTES] = {0};
  /* §6.2 byte 00: `01` sequential access, or `7F` when the LUN is not 0. */
  data[0] = lun == 0u ? AP_EXB8200_DEVICE_TYPE_SEQUENTIAL
                      : AP_EXB8200_DEVICE_TYPE_BAD_LUN;
  data[1] = AP_EXB8200_INQUIRY_RMB; /* removable, no device type qualifier */
  data[2] = AP_EXB8200_INQUIRY_ANSI_1;
  data[4] = AP_EXB8200_INQUIRY_ADDITIONAL;
  memcpy(&data[8], AP_EXB8200_VENDOR, 8u);
  memcpy(&data[16], AP_EXB8200_PRODUCT, 16u);
  memcpy(&data[32], AP_EXB8200_FIRMWARE, 4u);
  /* §6.3 bytes 36-55: "the ASCII representation of blanks". */
  memset(&data[36], ' ', 20u);

  /* §6.1: the allocation length is CDB byte 04, and a value of 0 "indicates
   * that no Inquiry data is to be transferred. This is not an error." */
  unsigned want = cdb[4];
  if (want > capacity) {
    want = capacity;
  }
  result->direction = AP_SCSI_DATA_IN;
  result->transferred = give(memory, buffer, data, want, capacity);
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_request_sense(ap_exb8200_t *drive, const uint8_t *cdb,
                                 const ap_scsi_memory_t *memory,
                                 uint32_t buffer, unsigned capacity,
                                 ap_scsi_result_t *result) {
  /* §15.1: "a value of 0 causes four bytes of sense data to be transferred.
   * All other values indicate the exact number of bytes to be transferred, up
   * to the maximum of 26." */
  unsigned want = cdb[4] == 0u ? AP_EXB8200_SENSE_MINIMUM : cdb[4];
  if (want > AP_EXB8200_SENSE_BYTES) {
    want = AP_EXB8200_SENSE_BYTES;
  }
  result->direction = AP_SCSI_DATA_IN;
  result->transferred =
      give(memory, buffer, drive->sense, want, capacity);
  /* §15.1 byte 05 bit 7: the RC bit resets the Read/Write Data Error Counter,
   * and "to reset the error counter, the initiator must read it; that is, the
   * initiator must allocate at least 19 bytes". */
  if ((cdb[5] & 0x80u) != 0u && want >= 19u) {
    drive->error_counter = 0u;
    drive->tracking_counter = 0u;
    drive->reread_counter = 0u;
  }
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_read_block_limits(ap_exb8200_t *drive,
                                     const ap_scsi_memory_t *memory,
                                     uint32_t buffer, unsigned capacity,
                                     ap_scsi_result_t *result) {
  uint8_t data[6] = {0};
  /* §12.1: 240 KB, or 160 KB when No Disconnect is set; minimum one byte. */
  const uint32_t maximum = (drive->vendor_flags & AP_EXB8200_VU_ND) != 0u
                               ? AP_EXB8200_BLOCK_MAX_ND
                               : AP_EXB8200_BLOCK_MAX;
  data[1] = (uint8_t)(maximum >> 16);
  data[2] = (uint8_t)(maximum >> 8);
  data[3] = (uint8_t)maximum;
  data[4] = (uint8_t)(AP_EXB8200_BLOCK_MIN >> 8);
  data[5] = (uint8_t)AP_EXB8200_BLOCK_MIN;
  result->direction = AP_SCSI_DATA_IN;
  result->transferred = give(memory, buffer, data, sizeof data, capacity);
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_read(ap_exb8200_t *drive, const uint8_t *cdb,
                        const ap_scsi_memory_t *memory, uint32_t buffer,
                        unsigned capacity, ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  const bool fixed = (cdb[1] & 0x01u) != 0u;
  const bool sili = (cdb[1] & 0x02u) != 0u;
  /* §11.3: "the SILI bit is valid only in variable read mode. If a fixed read
   * operation is specified and the SILI bit is set, Check Condition ... sense
   * key Illegal Request." */
  if (fixed && sili) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  /* §8.5's cross-command rule: Fixed set with a block length of 0, or Fixed
   * clear with a block length other than 0, is an error. */
  if (fixed == (drive->block_length == 0u)) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const uint32_t length = be24(&cdb[2]);
  result->direction = AP_SCSI_DATA_IN;
  if (length == 0u) {
    /* §11.1: "when the value for the Transfer Length field is 0, no data is
     * transferred and the current position on the tape is not changed. A value
     * of 0 in these bytes is not an error." */
    return AP_SCSI_STATUS_GOOD;
  }

  const unsigned wanted_blocks = fixed ? (unsigned)length : 1u;
  unsigned done = 0u;
  uint32_t at_buffer = buffer;
  unsigned moved = 0u;

  while (done < wanted_blocks) {
    if (drive->where == AP_EXB8200_AFTER_WRITE) {
      /* §24.11: after a write, a READ is Illegal Request -- the head is in the
       * gap track, not at end of data. */
      result->transferred = moved;
      return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, true,
                  (int32_t)(wanted_blocks - done), AP_EXB8200_ASC_NONE,
                  AP_EXB8200_ASCQ_NONE);
    }
    if (drive->at >= drive->media.records) {
      /* §11.3: blank tape ends the read with Blank Check. §24.2 adds EOM and
       * LBOT when the tape has never been written. */
      result->transferred = moved;
      const uint8_t flags =
          drive->where == AP_EXB8200_AT_LBOT ? AP_EXB8200_SENSE_EOM : 0u;
      return fail(drive, result, AP_EXB8200_KEY_BLANK_CHECK, flags, true,
                  (int32_t)(wanted_blocks - done), AP_EXB8200_ASC_NONE,
                  AP_EXB8200_ASCQ_NONE);
    }
    const ap_exb8200_record_t *record = &drive->media.record[drive->at];
    if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
      /* §11.3: "the read operation is terminated and the write/read head is
       * positioned on the EOT side of the filemark ... the FMK bit is set to 1
       * and the sense key is set to No Sense". Setting SILI does not suppress
       * it. */
      drive->at++;
      drive->where = AP_EXB8200_IN_DATA;
      result->transferred = moved;
      return fail(drive, result, AP_EXB8200_KEY_NO_SENSE, AP_EXB8200_SENSE_FMK,
                  true, (int32_t)(wanted_blocks - done), AP_EXB8200_ASC_NONE,
                  AP_EXB8200_ASCQ_NONE);
    }

    const uint32_t on_tape = record->length;
    const uint32_t asked = fixed ? drive->block_length : length;
    const uint32_t take = on_tape < asked ? on_tape : asked;
    moved += give(memory, at_buffer, &drive->media.data[record->offset],
                  (unsigned)take, capacity - moved);
    at_buffer += take;
    drive->at++;
    drive->where = AP_EXB8200_IN_DATA;
    done++;

    if (on_tape != asked) {
      /* §11.3's Illegal Length Indication. The Information bytes hold "the
       * difference in bytes between the requested length in the CDB and the
       * actual length of the logical block", and SILI suppresses *this* one
       * and only this one. */
      if (sili) {
        result->transferred = moved;
        result->short_transfer = on_tape < asked;
        return AP_SCSI_STATUS_GOOD;
      }
      result->transferred = moved;
      return fail(drive, result, AP_EXB8200_KEY_NO_SENSE, AP_EXB8200_SENSE_ILI,
                  true, (int32_t)asked - (int32_t)on_tape,
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
  }
  result->transferred = moved;
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_write(ap_exb8200_t *drive, const uint8_t *cdb,
                         const ap_scsi_memory_t *memory, uint32_t buffer,
                         unsigned capacity, ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  if (!drive->media.writable) {
    /* §21.5: Data Protect with the WP bit, which the sense builder sets from
     * the medium itself. */
    return fail(drive, result, AP_EXB8200_KEY_DATA_PROTECT, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const bool fixed = (cdb[1] & 0x01u) != 0u;
  if (fixed == (drive->block_length == 0u)) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const uint32_t length = be24(&cdb[2]);
  result->direction = AP_SCSI_DATA_OUT;
  if (length == 0u) {
    /* §21.1: "when the value for the Transfer Length field is 0, no data is
     * written to tape. A value of 0 is not an error." */
    return AP_SCSI_STATUS_GOOD;
  }
  if (!writable_here(drive)) {
    /* §21.5's Illegal Tape Position. */
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const uint32_t block = fixed ? drive->block_length : length;
  const uint32_t maximum = (drive->vendor_flags & AP_EXB8200_VU_ND) != 0u
                               ? AP_EXB8200_BLOCK_MAX_ND
                               : AP_EXB8200_BLOCK_MAX;
  if (!fixed && (block < AP_EXB8200_BLOCK_MIN || block > maximum)) {
    /* §21.5: "when the EXB-8200 is operating in variable mode and the Transfer
     * Length is not within the specified limits (see READ BLOCK LIMITS)". */
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }

  truncate_here(drive);
  const unsigned blocks = fixed ? (unsigned)length : 1u;
  uint32_t at_buffer = buffer;
  unsigned moved = 0u;
  for (unsigned i = 0; i < blocks; i++) {
    if (drive->media.records >= drive->media.capacity ||
        drive->media.data_used + block > drive->media.data_bytes) {
      /* §21.5's PEOT: the write terminates, EOM and PEOT are set, and the key
       * is Volume Overflow when data remains -- which it does, because the
       * host asked for more blocks than the medium can take. */
      drive->where = AP_EXB8200_AT_PEOT;
      result->transferred = moved;
      return fail(drive, result, AP_EXB8200_KEY_VOLUME_OVERFLOW,
                  AP_EXB8200_SENSE_EOM, true, (int32_t)(blocks - i),
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    ap_exb8200_record_t *record = &drive->media.record[drive->media.records];
    record->kind = AP_EXB8200_RECORD_DATA;
    record->offset = drive->media.data_used;
    record->length = block;
    record->short_mark = false;
    if (memory != nullptr) {
      for (uint32_t b = 0; b < block && moved < capacity; b++, moved++) {
        drive->media.data[record->offset + b] =
            memory->read(memory->context, at_buffer + b);
      }
    }
    at_buffer += block;
    drive->media.data_used += block;
    drive->media.records++;
    drive->at = drive->media.records;
  }
  /* §21.3: the drive is in write mode until a REWIND, UNLOAD, LOAD, SPACE or
   * WRITE FILEMARKS, and §23.7 puts a gap track after the last data track --
   * which is what makes a following READ Illegal Request rather than Blank
   * Check, §24.11. */
  drive->where = AP_EXB8200_AFTER_WRITE;
  result->transferred = moved;
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_write_filemarks(ap_exb8200_t *drive, const uint8_t *cdb,
                                   ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  if (!drive->media.writable) {
    return fail(drive, result, AP_EXB8200_KEY_DATA_PROTECT, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const uint32_t count = be24(&cdb[2]);
  /* §22.1 byte 05 bit 7: "when this bit is set to 1, only short filemarks are
   * written ... when 0, only long filemarks". */
  const bool short_mark = (cdb[5] & 0x80u) != 0u;
  if (count == 0u) {
    /* §22.1: "a 0 in this field indicates that no filemarks are to be written.
     * A value of 0 is not an error. Instead, all buffered data is written to
     * tape." This model writes through, so the flush is a no-op -- but it
     * still leaves write mode, §21.3. */
    drive->where = drive->at == 0u ? AP_EXB8200_AT_LBOT : AP_EXB8200_IN_DATA;
    return AP_SCSI_STATUS_GOOD;
  }
  if (!writable_here(drive) && drive->where != AP_EXB8200_AFTER_WRITE) {
    /* §22.2's Illegal Tape Position. A position just after this drive's own
     * write is legal -- §24.11 lets WRITE FILEMARKS follow a WRITE. */
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  if (drive->where != AP_EXB8200_AFTER_WRITE) {
    truncate_here(drive);
  }
  for (uint32_t i = 0; i < count; i++) {
    if (drive->media.records >= drive->media.capacity) {
      /* §22.2's PEOT: "if filemarks remain to be written, Check Condition ...
       * the sense key is set to Volume Overflow, the EOM and PEOT bits are set
       * to 1, and the LBOT bit is 0. The value for the Information bytes
       * indicates the number of unwritten filemarks." */
      drive->where = AP_EXB8200_AT_PEOT;
      return fail(drive, result, AP_EXB8200_KEY_VOLUME_OVERFLOW,
                  AP_EXB8200_SENSE_EOM, true, (int32_t)(count - i),
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    ap_exb8200_record_t *record = &drive->media.record[drive->media.records];
    record->kind = AP_EXB8200_RECORD_FILEMARK;
    record->offset = 0u;
    record->length = 0u;
    record->short_mark = short_mark;
    drive->media.records++;
    drive->at = drive->media.records;
  }
  drive->where = AP_EXB8200_IN_DATA;
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_space(ap_exb8200_t *drive, const uint8_t *cdb,
                         ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  /* §19.1: "`00` space over logical blocks, `01` space over filemarks, `10`
   * and `11` not supported". */
  const unsigned code = cdb[1] & 0x03u;
  if (code > 1u) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  const bool filemarks = code == 1u;
  int32_t count = signed24(&cdb[2]);
  if (count == 0) {
    /* §19.1: "a value of 0 causes no tape movement. This is not an error." */
    return AP_SCSI_STATUS_GOOD;
  }
  /* §21.3: SPACE takes the drive out of write mode. */
  if (drive->where == AP_EXB8200_AFTER_WRITE) {
    drive->where = AP_EXB8200_IN_DATA;
  }

  const bool forward = count > 0;
  int32_t remaining = forward ? count : -count;
  while (remaining > 0) {
    if (forward) {
      if (drive->at >= drive->media.records) {
        /* §19.4: blank tape while spacing over blocks is Blank Check; PEOT
         * while spacing over filemarks is Medium Error with EOM and PEOT.
         * Reaching the end of the records is the end of data either way, and
         * §24.2 gives a forward space filemark past the last one PEOT. */
        if (filemarks) {
          drive->where = AP_EXB8200_AT_PEOT;
          return fail(drive, result, AP_EXB8200_KEY_MEDIUM_ERROR,
                      AP_EXB8200_SENSE_EOM, true, remaining,
                      AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
        }
        return fail(drive, result, AP_EXB8200_KEY_BLANK_CHECK, 0u, true,
                    remaining, AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
      }
      const ap_exb8200_record_t *record = &drive->media.record[drive->at];
      drive->at++;
      drive->where = AP_EXB8200_IN_DATA;
      if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
        if (filemarks) {
          remaining--;
          continue;
        }
        /* §19.4: "when the EXB-8200 encounters a filemark while spacing over
         * logical blocks, the space operation is terminated ... sense key No
         * Sense and the FMK bit set to 1", positioned on the EOT side. */
        return fail(drive, result, AP_EXB8200_KEY_NO_SENSE,
                    AP_EXB8200_SENSE_FMK, true, remaining, AP_EXB8200_ASC_NONE,
                    AP_EXB8200_ASCQ_NONE);
      }
      if (!filemarks) {
        remaining--;
      }
      continue;
    }
    /* Backward. */
    if (drive->at == 0u) {
      /* §19.4: "when the EXB-8200 encounters PBOT or LBOT ... the tape is
       * positioned at LBOT ... sense key No Sense and the EOM bit and LBOT bit
       * set to 1". */
      drive->where = AP_EXB8200_AT_LBOT;
      return fail(drive, result, AP_EXB8200_KEY_NO_SENSE, AP_EXB8200_SENSE_EOM,
                  true, -remaining, AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    drive->at--;
    drive->where = drive->at == 0u ? AP_EXB8200_AT_LBOT : AP_EXB8200_IN_DATA;
    const ap_exb8200_record_t *record = &drive->media.record[drive->at];
    if (record->kind == AP_EXB8200_RECORD_FILEMARK) {
      if (filemarks) {
        remaining--;
        continue;
      }
      return fail(drive, result, AP_EXB8200_KEY_NO_SENSE, AP_EXB8200_SENSE_FMK,
                  true, -remaining, AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    if (!filemarks) {
      remaining--;
    }
  }
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_rewind(ap_exb8200_t *drive, ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  /* §17: "causes the EXB-8200 to rewind the cartridge tape to LBOT", flushing
   * the buffer first -- which this model has already done. */
  drive->at = 0u;
  drive->where = AP_EXB8200_AT_LBOT;
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_load_unload(ap_exb8200_t *drive, const uint8_t *cdb,
                               ap_scsi_result_t *result) {
  /* §7.1 byte 04 bit 0: 1 load, 0 unload. */
  const bool load = (cdb[4] & 0x01u) != 0u;
  if (load) {
    if (!drive->present) {
      /* §7.3: "if the Load bit is set to 1 and a data cartridge is not in the
       * EXB-8200 with the door closed, Check Condition ... Not Ready with the
       * TNP bit set to 1." */
      return fail(drive, result, AP_EXB8200_KEY_NOT_READY, 0u, false, 0,
                  AP_EXB8200_ASC_NOT_READY, AP_EXB8200_ASCQ_NOT_MOUNTED);
    }
    /* §7.2: loaded or not, the tape ends at LBOT, and "if the tape is already
     * at LBOT, the tape is not moved and Good status is returned". */
    drive->loaded = true;
    drive->at = 0u;
    drive->where = AP_EXB8200_AT_LBOT;
    return AP_SCSI_STATUS_GOOD;
  }
  /* §7.2's unload. "Issuing the UNLOAD command to an EXB-8200 that is already
   * unloaded is not an error." */
  drive->loaded = false;
  drive->at = 0u;
  drive->where = AP_EXB8200_IN_DATA;
  /* §7: the cartridge is ejected "unless PREVENT MEDIUM REMOVAL has been
   * issued, in which case the tape is rewound and unloaded but not ejected". */
  if (!drive->prevented) {
    drive->present = false;
  }
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_erase(ap_exb8200_t *drive, const uint8_t *cdb,
                         ap_scsi_result_t *result) {
  const uint8_t ready = medium_ready(drive, result);
  if (ready != AP_SCSI_STATUS_GOOD) {
    return ready;
  }
  if (!drive->media.writable) {
    /* §5.3: Data Protect with the WP bit. */
    return fail(drive, result, AP_EXB8200_KEY_DATA_PROTECT, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  /* §5.1: "the EXB-8200 supports only the long erase operation ... the valid
   * value for the Long bit is 1. If the Long bit is not set, the EXB-8200
   * accepts the ERASE command but no operation is performed." */
  if ((cdb[1] & 0x01u) == 0u) {
    return AP_SCSI_STATUS_GOOD;
  }
  if (!writable_here(drive)) {
    /* §5.3's Illegal Tape Position: LBOT, blank tape, or the BOT side of a
     * long filemark. */
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  truncate_here(drive);
  /* §5: "when the erase operation is successfully completed, a rewind
   * automatically occurs". */
  drive->at = 0u;
  drive->where = AP_EXB8200_AT_LBOT;
  return AP_SCSI_STATUS_GOOD;
}

/* The 17 bytes MODE SENSE returns, §9.1. */
static void mode_bytes(const ap_exb8200_t *drive, uint8_t *out) {
  memset(out, 0, AP_EXB8200_MODE_BYTES);
  /* Header. Byte 00 is the sense data length "excluding this byte". */
  out[0] = (uint8_t)(AP_EXB8200_MODE_BYTES - 1u);
  out[1] = drive->media.medium_type;
  out[2] = (uint8_t)((drive->media.writable ? 0u : AP_EXB8200_MODE_WP) |
                     ((drive->buffered ? AP_EXB8200_BUFFERED_ON
                                       : AP_EXB8200_BUFFERED_NONE)
                      << AP_EXB8200_MODE_BUFFERED_SHIFT));
  out[3] = AP_EXB8200_MODE_DESCRIPTOR;
  /* Block descriptor. §9.3: density code 0, and the block count is "the number
   * of physical blocks that can be written from LBOT to LEOT plus the number
   * of blocks occupied by LBOT (`500h`)". */
  const uint32_t blocks = drive->media.blocks_to_leot + AP_EXB8200_LBOT_BLOCKS;
  out[4] = 0u;
  out[5] = (uint8_t)(blocks >> 16);
  out[6] = (uint8_t)(blocks >> 8);
  out[7] = (uint8_t)blocks;
  out[9] = (uint8_t)(drive->block_length >> 16);
  out[10] = (uint8_t)(drive->block_length >> 8);
  out[11] = (uint8_t)drive->block_length;
  /* Vendor unique, §9.4. */
  out[12] = drive->vendor_flags;
  out[13] = drive->p5 ? AP_EXB8200_VU_P5 : 0u;
  out[14] = drive->motion_threshold;
  out[15] = drive->reconnect_threshold;
  out[16] = drive->gap_threshold;
}

static uint8_t cmd_mode_sense(ap_exb8200_t *drive, const uint8_t *cdb,
                              const ap_scsi_memory_t *memory, uint32_t buffer,
                              unsigned capacity, ap_scsi_result_t *result) {
  uint8_t data[AP_EXB8200_MODE_BYTES];
  mode_bytes(drive, data);
  unsigned want = cdb[4];
  if (want > sizeof data) {
    want = sizeof data;
  }
  result->direction = AP_SCSI_DATA_IN;
  result->transferred = give(memory, buffer, data, want, capacity);
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_mode_select(ap_exb8200_t *drive, const uint8_t *cdb,
                               const ap_scsi_memory_t *memory, uint32_t buffer,
                               ap_scsi_result_t *result) {
  /* §8.1 and Table 8-1: the parameter list length is `00h` to `11h`, and only
   * 0, 4 through 9, and `0Ch` through `11h` are legal -- a header without its
   * block descriptor may carry vendor bytes, but a partial descriptor may
   * not. */
  const unsigned length = cdb[4];
  const bool legal = length == 0u || (length >= 4u && length <= 9u) ||
                     (length >= 0x0Cu && length <= 0x11u);
  if (!legal) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  result->direction = AP_SCSI_DATA_OUT;
  if (length == 0u || memory == nullptr) {
    /* "A value of 0 for the Parameter List Length byte is not an error." */
    return AP_SCSI_STATUS_GOOD;
  }
  uint8_t list[0x11u] = {0};
  for (unsigned i = 0; i < length; i++) {
    list[i] = memory->read(memory->context, buffer + i);
  }
  result->transferred = length;

  /* Header byte 02. §8.2: buffered mode is `000` or `001` and nothing else,
   * and the speed field's "valid value is `0h`". */
  const unsigned buffered = (list[2] & AP_EXB8200_MODE_BUFFERED_MASK) >>
                            AP_EXB8200_MODE_BUFFERED_SHIFT;
  if (buffered > AP_EXB8200_BUFFERED_ON ||
      (list[2] & AP_EXB8200_MODE_SPEED_MASK) != 0u) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  /* Byte 03: "the valid values for this byte are `00h` and `08h`". */
  if (list[3] != 0u && list[3] != AP_EXB8200_MODE_DESCRIPTOR) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  unsigned vendor_at = AP_EXB8200_MODE_HEADER;
  if (list[3] == AP_EXB8200_MODE_DESCRIPTOR) {
    /* §8.3: the only accepted density code is 0 and the only accepted number
     * of blocks is 0. */
    if (list[4] != 0u || list[5] != 0u || list[6] != 0u || list[7] != 0u) {
      return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    drive->block_length = be24(&list[9]);
    vendor_at += AP_EXB8200_MODE_DESCRIPTOR;
  }
  drive->buffered = buffered == AP_EXB8200_BUFFERED_ON;

  /* The vendor bytes are optional and are taken one at a time, which is what
   * Table 8-1's lengths 05 through 09 and 0D through 11 mean. */
  const unsigned vendor = length > vendor_at ? length - vendor_at : 0u;
  if (vendor >= 1u) {
    drive->vendor_flags = list[vendor_at];
  }
  if (vendor >= 2u) {
    drive->p5 = (list[vendor_at + 1u] & AP_EXB8200_VU_P5) != 0u;
  }
  if (vendor >= 3u) {
    /* §8.4: "value limits are `20h` to `D0h`" for both thresholds. */
    const uint8_t motion = list[vendor_at + 2u];
    if (motion < AP_EXB8200_THRESHOLD_MIN ||
        motion > AP_EXB8200_THRESHOLD_MAX) {
      return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    drive->motion_threshold = motion;
  }
  if (vendor >= 4u) {
    const uint8_t reconnect = list[vendor_at + 3u];
    if (reconnect < AP_EXB8200_THRESHOLD_MIN ||
        reconnect > AP_EXB8200_THRESHOLD_MAX) {
      return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
    drive->reconnect_threshold = reconnect;
  }
  if (vendor >= 5u) {
    /* §8.4: "valid values ... are in the range `00h` to `FFh`. However, any
     * value greater than `07h` is treated as `07h`." */
    const uint8_t gap = list[vendor_at + 4u];
    drive->gap_threshold =
        gap > AP_EXB8200_GAP_THRESHOLD_CAP ? AP_EXB8200_GAP_THRESHOLD_CAP : gap;
  }
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_receive_diagnostic(ap_exb8200_t *drive, const uint8_t *cdb,
                                      const ap_scsi_memory_t *memory,
                                      uint32_t buffer, unsigned capacity,
                                      ap_scsi_result_t *result) {
  uint8_t data[AP_EXB8200_DIAGNOSTIC_BYTES] = {0};
  data[0] = (uint8_t)(AP_EXB8200_DIAGNOSTIC_ADDITIONAL >> 8);
  data[1] = (uint8_t)AP_EXB8200_DIAGNOSTIC_ADDITIONAL;
  data[2] = 0u; /* no counter has overflowed */
  data[3] = drive->tracking_counter;
  data[4] = drive->reread_counter;
  data[5] = drive->write_recovery_counter;
  /* §13: "this command is the second of two ... must be preceded by the SEND
   * DIAGNOSTIC command. If not, the data returned is not valid." Not an error,
   * so the bytes are returned either way and only their validity differs --
   * which is why `diagnostic_ready` is state and not a refusal. */
  unsigned want = (unsigned)((cdb[3] << 8) | cdb[4]);
  if (want > sizeof data) {
    want = sizeof data;
  }
  result->direction = AP_SCSI_DATA_IN;
  result->transferred = give(memory, buffer, data, want, capacity);
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_send_diagnostic(ap_exb8200_t *drive, const uint8_t *cdb,
                                   ap_scsi_result_t *result) {
  /* §18.3's Table 18-1: five legal combinations of SelfTest, DevOfL and
   * UnitOfL, and "any combinations of values other than those defined ...
   * are invalid". */
  const unsigned test = cdb[1] & 0x07u;
  switch (test) {
  case AP_EXB8200_TEST_COUNTERS:
  case AP_EXB8200_TEST_POWER_ON:
  case AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_NO_TAPE:
  case AP_EXB8200_TEST_POWER_ON_TAPE:
  case AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_TAPE:
    break;
  default:
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  /* §18.3's test setups. The two tape tests need a cartridge; the two
   * without-tape tests need none, and running one with a tape in is an
   * "invalid test setup", Illegal Request. */
  const bool wants_tape = test == AP_EXB8200_TEST_POWER_ON_TAPE ||
                          test == AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_TAPE;
  const bool without_tape =
      test == AP_EXB8200_TEST_POWER_ON ||
      test == AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_NO_TAPE;
  if (wants_tape && !drive->loaded) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  if (without_tape && drive->present) {
    return fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
  }
  if (wants_tape && !drive->media.writable) {
    /* §18.4: "an attempt to execute the functional tape tests in Tests 101 or
     * 111 with the cartridge set for Write Protect returns Check Condition ...
     * Data Protect." */
    if (test == AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_TAPE) {
      return fail(drive, result, AP_EXB8200_KEY_DATA_PROTECT, 0u, false, 0,
                  AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    }
  }
  /* §18.3's Test 110 and 111 leave the tape at LBOT on success. */
  if (wants_tape) {
    drive->at = 0u;
    drive->where = AP_EXB8200_AT_LBOT;
  }
  drive->diagnostic_ready = true;
  return AP_SCSI_STATUS_GOOD;
}

static uint8_t cmd_prevent_allow(ap_exb8200_t *drive, const uint8_t *cdb,
                                 ap_scsi_result_t *result) {
  (void)result;
  /* §10.1 byte 04 bit 0. */
  drive->prevented = (cdb[4] & 0x01u) != 0u;
  return AP_SCSI_STATUS_GOOD;
}

/* ---- Dispatch ------------------------------------------------------------ */

/* §4.4's rules that apply to every command, checked before any of them runs. */
static bool common_errors(ap_exb8200_t *drive, uint8_t lun, const uint8_t *cdb,
                          ap_scsi_result_t *result, uint8_t *status) {
  const uint8_t opcode = cdb[0];
  /* §4.3: "the EXB-8200 supports Group 0 commands only. All commands must
   * contain 0 in this field; any other value results in an error condition." */
  if (ap_scsi_cdb_length(opcode) == 0u) {
    *status = fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                   AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    return true;
  }
  /* §4.3: "since the EXB-8200 does not support multiple devices, the LUN must
   * be 0 at all times ... if the Identify message specifies a LUN other than
   * 0, or if the CDB specifies a LUN other than 0, the EXB-8200 reports an
   * error." INQUIRY is the exception -- §6.2 gives it device type `7F`
   * instead, which is how a host discovers the LUN is invalid. */
  const uint8_t cdb_lun = (uint8_t)((cdb[1] >> 5) & 0x07u);
  if ((lun != 0u || cdb_lun != 0u) && opcode != AP_EXB8200_CMD_INQUIRY) {
    *status = fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                   AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    return true;
  }
  /* §4.4: "if the Link or Flag bits are not 0, the command operation is
   * terminated. Check Condition ... Illegal Request." */
  if ((cdb[5] & 0x03u) != 0u) {
    *status = fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
                   AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    return true;
  }
  return false;
}

/* Whether this opcode is one of the eighteen. */
static bool supported(uint8_t opcode) {
  switch (opcode) {
  case AP_EXB8200_CMD_TEST_UNIT_READY:
  case AP_EXB8200_CMD_REWIND:
  case AP_EXB8200_CMD_REQUEST_SENSE:
  case AP_EXB8200_CMD_READ_BLOCK_LIMITS:
  case AP_EXB8200_CMD_READ:
  case AP_EXB8200_CMD_WRITE:
  case AP_EXB8200_CMD_WRITE_FILEMARKS:
  case AP_EXB8200_CMD_SPACE:
  case AP_EXB8200_CMD_INQUIRY:
  case AP_EXB8200_CMD_MODE_SELECT:
  case AP_EXB8200_CMD_RESERVE_UNIT:
  case AP_EXB8200_CMD_RELEASE_UNIT:
  case AP_EXB8200_CMD_ERASE:
  case AP_EXB8200_CMD_MODE_SENSE:
  case AP_EXB8200_CMD_LOAD_UNLOAD:
  case AP_EXB8200_CMD_RECEIVE_DIAGNOSTIC:
  case AP_EXB8200_CMD_SEND_DIAGNOSTIC:
  case AP_EXB8200_CMD_PREVENT_ALLOW:
    return true;
  default:
    return false;
  }
}

static bool exb_execute(void *device, uint8_t lun, const uint8_t *cdb,
                        unsigned cdb_length, const ap_scsi_memory_t *memory,
                        uint32_t buffer, unsigned capacity,
                        ap_scsi_result_t *result) {
  ap_exb8200_t *drive = (ap_exb8200_t *)device;
  if (cdb_length < AP_SCSI_CDB_GROUP_0) {
    return false;
  }
  const uint8_t opcode = cdb[0];
  drive->commands++;

  /* §16: "if a reserved EXB-8200 receives any command other than INQUIRY or
   * REQUEST SENSE from another initiator, the command is rejected" with
   * Reservation Conflict -- and a REQUEST SENSE **with the RC bit set** is
   * rejected too, §15.1's Important note, because it would reset a counter the
   * owning initiator is using. This model tells initiators apart by the ID the
   * bus selected with, which the ASC's own ID stands in for; a second
   * initiator is not reachable on this machine, so the branch exists for the
   * rule rather than for a caller. */
  (void)lun;

  uint8_t status = AP_SCSI_STATUS_GOOD;
  if (common_errors(drive, lun, cdb, result, &status)) {
    return true;
  }
  if (!supported(opcode)) {
    /* §4.4's Illegal Operation Code. */
    (void)fail(drive, result, AP_EXB8200_KEY_ILLEGAL_REQUEST, 0u, false, 0,
               AP_EXB8200_ASC_NONE, AP_EXB8200_ASCQ_NONE);
    return true;
  }

  /* §15.2 and Table 15-1's Unit Attention row: the first command after a reset
   * or a cartridge change, **other than INQUIRY or REQUEST SENSE**, gets Check
   * Condition and "the EXB-8200 does not perform the requested command". */
  const bool exempt = opcode == AP_EXB8200_CMD_INQUIRY ||
                      opcode == AP_EXB8200_CMD_REQUEST_SENSE;
  if (!exempt && drive->unit_attention) {
    /* Table 15-1: the key is cleared "after it receives the next command from
     * the initiator", so the *condition* is reported exactly once while the
     * sense it built stays for the REQUEST SENSE that follows. */
    drive->unit_attention = false;
    drive->check_conditions++;
    result->status = AP_SCSI_STATUS_CHECK_CONDITION;
    return true;
  }

  switch (opcode) {
  case AP_EXB8200_CMD_TEST_UNIT_READY:
    status = cmd_test_unit_ready(drive, result);
    break;
  case AP_EXB8200_CMD_REWIND:
    status = cmd_rewind(drive, result);
    break;
  case AP_EXB8200_CMD_REQUEST_SENSE:
    status = cmd_request_sense(drive, cdb, memory, buffer, capacity, result);
    /* §15.3: the sense survives a REQUEST SENSE, so it is not cleared here. */
    return true;
  case AP_EXB8200_CMD_READ_BLOCK_LIMITS:
    status = cmd_read_block_limits(drive, memory, buffer, capacity, result);
    break;
  case AP_EXB8200_CMD_READ:
    status = cmd_read(drive, cdb, memory, buffer, capacity, result);
    break;
  case AP_EXB8200_CMD_WRITE:
    status = cmd_write(drive, cdb, memory, buffer, capacity, result);
    break;
  case AP_EXB8200_CMD_WRITE_FILEMARKS:
    status = cmd_write_filemarks(drive, cdb, result);
    break;
  case AP_EXB8200_CMD_SPACE:
    status = cmd_space(drive, cdb, result);
    break;
  case AP_EXB8200_CMD_INQUIRY:
    status = cmd_inquiry(lun, cdb, memory, buffer, capacity, result);
    /* §6.4: "following a Check Condition status on an INQUIRY command, the
     * sense data created prior to the INQUIRY command remains valid", so this
     * one does not clear it either. */
    return true;
  case AP_EXB8200_CMD_MODE_SELECT:
    status = cmd_mode_select(drive, cdb, memory, buffer, result);
    break;
  case AP_EXB8200_CMD_RESERVE_UNIT:
    /* §16: available only for 2600-level MX code and above, which this model
     * carries -- `[EXBPS]`'s revision history dates both commands to it. */
    drive->reserved = true;
    break;
  case AP_EXB8200_CMD_RELEASE_UNIT:
    drive->reserved = false;
    break;
  case AP_EXB8200_CMD_ERASE:
    status = cmd_erase(drive, cdb, result);
    break;
  case AP_EXB8200_CMD_MODE_SENSE:
    status = cmd_mode_sense(drive, cdb, memory, buffer, capacity, result);
    break;
  case AP_EXB8200_CMD_LOAD_UNLOAD:
    status = cmd_load_unload(drive, cdb, result);
    break;
  case AP_EXB8200_CMD_RECEIVE_DIAGNOSTIC:
    status =
        cmd_receive_diagnostic(drive, cdb, memory, buffer, capacity, result);
    break;
  case AP_EXB8200_CMD_SEND_DIAGNOSTIC:
    status = cmd_send_diagnostic(drive, cdb, result);
    break;
  case AP_EXB8200_CMD_PREVENT_ALLOW:
    status = cmd_prevent_allow(drive, cdb, result);
    break;
  default:
    break;
  }

  /* §15: "the EXB-8200 clears this sense data after receiving any subsequent
   * command that is not REQUEST SENSE or INQUIRY". A command that *built* new
   * sense keeps it, which is why this tests the status rather than the
   * opcode. */
  if (status == AP_SCSI_STATUS_GOOD) {
    drive->sense_valid = false;
    memset(drive->sense, 0, sizeof drive->sense);
  }
  result->status = status;
  return true;
}

static void exb_reset(void *device) {
  ap_exb8200_reset((ap_exb8200_t *)device);
}

static bool exb_present(const void *device) {
  /* A drive answers selection whenever it is powered, cartridge or not: §20.2
   * gives a cartridge-less drive a *status*, which means it was selected. */
  (void)device;
  return true;
}

ap_scsi_target_t ap_exb8200_target(ap_exb8200_t *drive) {
  return (ap_scsi_target_t){.device = drive,
                            .execute = exb_execute,
                            .reset = exb_reset,
                            .present = exb_present};
}
