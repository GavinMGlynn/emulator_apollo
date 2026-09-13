/* EXABYTE EXB-8200 8mm cartridge tape subsystem: the SCSI target.
 *
 * `[EXB]` *EXB-8200 User's Manual* 510006-007, 141 pages, and `[EXBPS]`
 * *EXB-8200 Product Specification* 510005-006, 74 pages. Both walked whole --
 * `docs/references/EXB8200_WALK.md`, 215/215.
 *
 * **Why this part and not another.** No hardware document on this shelf names
 * the DS5500's SCSI target. The machine's own software does:
 * `/sys/mgrs/rmt_scsi`, extracted off the SR10.4 volume with
 * `tools/awd_read.py`, carries a four-entry INQUIRY vendor-plus-product table
 * and matches `EXABYTE EXB-8200        `. Third use of the method in
 * `the-guest-driver-names-the-part`, after the WD7000-ASC and the SC-499.
 *
 * ## The split
 *
 * `ap_scsi` is the bus and this is a device on it, the same shape `ap_sc499`
 * and `ap_qic` have. Everything here is `[EXB]`'s; nothing here knows what a
 * mailbox is.
 *
 * ## What a tape is, in this model
 *
 * A **sequence of records**, each either a data block of any length from 1 byte
 * to 240 KB or a filemark of one of the two kinds. That is the logical format
 * `[EXB]` §4.3 and `[EXBPS]` §4.3 describe, and it is the level the command set
 * addresses: READ and WRITE name blocks, SPACE names blocks or filemarks, and
 * no command can observe a physical block, a gap byte or a track.
 *
 * The physical format is modelled only where it is host-visible, and it is in
 * exactly two places. `[EXBPS]` §4.3.5 makes a filemark an erase gap plus an
 * **11-track analog tape mark** plus a **10-track digital tape mark**, so a long
 * one occupies 270 tracks and a short one 60 -- the 2,160 KB and 480 KB `[EXB]`
 * ch. 22 quotes. And `[EXB]` §23.5's 1,024-byte physical block is what makes a
 * logical block that is not a multiple of it waste the remainder. Both are
 * *capacity* facts, so they are computed rather than stored.
 */

#ifndef APOLLO_DEVICE_AP_EXB8200_H
#define APOLLO_DEVICE_AP_EXB8200_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_scsi.h"

/* ---- The command set, `[EXB]` Table 4-1 ---------------------------------- */

#define AP_EXB8200_CMD_TEST_UNIT_READY 0x00u
#define AP_EXB8200_CMD_REWIND 0x01u
#define AP_EXB8200_CMD_REQUEST_SENSE 0x03u
#define AP_EXB8200_CMD_READ_BLOCK_LIMITS 0x05u
#define AP_EXB8200_CMD_READ 0x08u
#define AP_EXB8200_CMD_WRITE 0x0Au
#define AP_EXB8200_CMD_WRITE_FILEMARKS 0x10u
#define AP_EXB8200_CMD_SPACE 0x11u
#define AP_EXB8200_CMD_INQUIRY 0x12u
#define AP_EXB8200_CMD_MODE_SELECT 0x15u
#define AP_EXB8200_CMD_RESERVE_UNIT 0x16u
#define AP_EXB8200_CMD_RELEASE_UNIT 0x17u
#define AP_EXB8200_CMD_ERASE 0x19u
#define AP_EXB8200_CMD_MODE_SENSE 0x1Au
#define AP_EXB8200_CMD_LOAD_UNLOAD 0x1Bu
#define AP_EXB8200_CMD_RECEIVE_DIAGNOSTIC 0x1Cu
#define AP_EXB8200_CMD_SEND_DIAGNOSTIC 0x1Du
#define AP_EXB8200_CMD_PREVENT_ALLOW 0x1Eu

/* Eighteen of them, `[EXB]` Table 4-1 and `[EXBPS]` Table 8-2. */
#define AP_EXB8200_COMMANDS 18u

/* ---- Sense, `[EXB]` §15.2 ------------------------------------------------ */

/* Twenty-six bytes of Error Class 7 extended sense. An allocation of 0
 * transfers **four** bytes, not none -- §15.1. */
#define AP_EXB8200_SENSE_BYTES 26u
#define AP_EXB8200_SENSE_MINIMUM 4u
/* Byte 07: "the value is `12h` for sense data generated for all commands". */
#define AP_EXB8200_ADDITIONAL_SENSE_LENGTH 0x12u
/* Byte 00: `Valid` in bit 7, error class always `7h`, error code always `0h`. */
#define AP_EXB8200_SENSE_VALID 0x80u
#define AP_EXB8200_SENSE_CLASS_7 0x70u

/* Byte 02's three flags and its sense key. */
#define AP_EXB8200_SENSE_FMK 0x80u
#define AP_EXB8200_SENSE_EOM 0x40u
#define AP_EXB8200_SENSE_ILI 0x20u
#define AP_EXB8200_SENSE_KEY_MASK 0x0Fu

/* Table 15-1. `1h`, `Ah`, `Ch` and `Eh` are "not used by the EXB-8200", and
 * `9h` is EXABYTE's own, raised by the `TMD` and `XFR` bits alone. */
#define AP_EXB8200_KEY_NO_SENSE 0x0u
#define AP_EXB8200_KEY_NOT_READY 0x2u
#define AP_EXB8200_KEY_MEDIUM_ERROR 0x3u
#define AP_EXB8200_KEY_HARDWARE_ERROR 0x4u
#define AP_EXB8200_KEY_ILLEGAL_REQUEST 0x5u
#define AP_EXB8200_KEY_UNIT_ATTENTION 0x6u
#define AP_EXB8200_KEY_DATA_PROTECT 0x7u
#define AP_EXB8200_KEY_BLANK_CHECK 0x8u
#define AP_EXB8200_KEY_EXABYTE 0x9u
#define AP_EXB8200_KEY_ABORTED_COMMAND 0xBu
#define AP_EXB8200_KEY_VOLUME_OVERFLOW 0xDu

/* Table 15-2: the four ASC/ASCQ pairs this drive defines and no others. */
#define AP_EXB8200_ASC_NONE 0x00u
#define AP_EXB8200_ASCQ_NONE 0x00u
#define AP_EXB8200_ASC_NOT_READY 0x04u
#define AP_EXB8200_ASCQ_NOT_MOUNTED 0x00u
#define AP_EXB8200_ASCQ_REWINDING 0x01u
#define AP_EXB8200_ASC_CANNOT_READ 0x30u
#define AP_EXB8200_ASCQ_INCOMPATIBLE 0x02u

/* Table 15-3's nineteen unit-sense bits, bytes 19, 20 and 21. */
#define AP_EXB8200_U19_PF 0x80u   /* power fail / reset since last status */
#define AP_EXB8200_U19_BPE 0x40u  /* SCSI bus parity error */
#define AP_EXB8200_U19_FBPE 0x20u /* formatted buffer parity error */
#define AP_EXB8200_U19_ME 0x10u   /* media error */
#define AP_EXB8200_U19_ECO 0x08u  /* error counter overflow */
#define AP_EXB8200_U19_TME 0x04u  /* tape motion error */
#define AP_EXB8200_U19_TNP 0x02u  /* tape not present */
#define AP_EXB8200_U19_LBOT 0x01u /* at logical beginning of tape */

#define AP_EXB8200_U20_XFR 0x80u  /* transfer abort error */
#define AP_EXB8200_U20_TMD 0x40u  /* tape mark detect error */
#define AP_EXB8200_U20_WP 0x20u   /* write protected */
#define AP_EXB8200_U20_FMKE 0x10u /* filemark error */
#define AP_EXB8200_U20_URE 0x08u  /* under run error */
#define AP_EXB8200_U20_WE1 0x04u  /* write error 1 */
#define AP_EXB8200_U20_SSE 0x02u  /* servo system error */
#define AP_EXB8200_U20_FE 0x01u   /* formatter error */

#define AP_EXB8200_U21_PEOT 0x04u /* at physical end of tape */
#define AP_EXB8200_U21_WSEB 0x02u /* write splice error, blank */
#define AP_EXB8200_U21_WSEO 0x01u /* write splice error, overshoot */

/* ---- Block limits, `[EXB]` §12.1 ----------------------------------------- */

#define AP_EXB8200_BLOCK_MAX 0x03C000u    /* 240 KB */
#define AP_EXB8200_BLOCK_MAX_ND 0x028000u /* 160 KB with No Disconnect */
#define AP_EXB8200_BLOCK_MIN 0x000001u    /* one byte */

/* ---- Mode parameters, `[EXB]` ch. 8 and 9 -------------------------------- */

/* MODE SENSE returns 17 bytes: a 4-byte header, an 8-byte block descriptor and
 * five vendor-unique bytes, §9.1. MODE SELECT takes the same shape. */
#define AP_EXB8200_MODE_BYTES 17u
#define AP_EXB8200_MODE_HEADER 4u
#define AP_EXB8200_MODE_DESCRIPTOR 8u
#define AP_EXB8200_MODE_VENDOR 5u

/* Header byte 02: `WP` in bit 7, buffered mode in 6-4, speed in 3-0. */
#define AP_EXB8200_MODE_WP 0x80u
#define AP_EXB8200_MODE_BUFFERED_MASK 0x70u
#define AP_EXB8200_MODE_BUFFERED_SHIFT 4u
#define AP_EXB8200_MODE_SPEED_MASK 0x0Fu
#define AP_EXB8200_BUFFERED_NONE 0u
#define AP_EXB8200_BUFFERED_ON 1u

/* Vendor byte 00's eight bits, §8.4. Every default is 0 except as noted. */
#define AP_EXB8200_VU_CT 0x80u  /* cartridge type, with P5 */
#define AP_EXB8200_VU_ND 0x20u  /* no disconnect during data transfer */
#define AP_EXB8200_VU_NBE 0x08u /* no busy enable */
#define AP_EXB8200_VU_EBD 0x04u /* even byte disconnect */
#define AP_EXB8200_VU_PE 0x02u  /* parity enable */
#define AP_EXB8200_VU_NAL 0x01u /* no autoload */
#define AP_EXB8200_VU_P5 0x01u  /* vendor byte 01 bit 0 */

/* §8.4's defaults, each stated there and repeated in §9.4. */
#define AP_EXB8200_BLOCK_LENGTH_DEFAULT 1024u /* `400h` */
#define AP_EXB8200_MOTION_THRESHOLD_DEFAULT 0x80u
#define AP_EXB8200_RECONNECT_THRESHOLD_DEFAULT 0xA0u
#define AP_EXB8200_GAP_THRESHOLD_DEFAULT 0x07u
#define AP_EXB8200_THRESHOLD_MIN 0x20u
#define AP_EXB8200_THRESHOLD_MAX 0xD0u
/* "any value greater than `07h` is treated as `07h`" */
#define AP_EXB8200_GAP_THRESHOLD_CAP 0x07u

/* ---- INQUIRY, `[EXB]` ch. 6 ---------------------------------------------- */

/* 56 bytes: five standard plus `33h` = 51 vendor unique. */
#define AP_EXB8200_INQUIRY_BYTES 56u
#define AP_EXB8200_INQUIRY_ADDITIONAL 0x33u
#define AP_EXB8200_DEVICE_TYPE_SEQUENTIAL 0x01u
/* "If the LUN in the CDB is not 0, the value returned is `7Fh`." */
#define AP_EXB8200_DEVICE_TYPE_BAD_LUN 0x7Fu
#define AP_EXB8200_INQUIRY_RMB 0x80u
#define AP_EXB8200_INQUIRY_ANSI_1 0x01u

/* Bytes 08-15 and 16-31, and the reason they come from the guest rather than
 * from the manual: `[EXB]`'s PDF text layer drops hyphens and renders the
 * part's own name "EXB 8200" in body text throughout, while `rmt_scsi`'s
 * matching table is byte-exact. See `EXB8200_WALK.md` ch. 6. */
#define AP_EXB8200_VENDOR "EXABYTE "
#define AP_EXB8200_PRODUCT "EXB-8200        "
/* Bytes 32-35, "the ASCII representation of the firmware revision level
 * followed by spaces (for example, `4.25`)". The example is the value, and it
 * is the one number in this part taken from an example -- named as such. */
#define AP_EXB8200_FIRMWARE "4.25"

/* ---- Diagnostics, `[EXB]` ch. 13 and 18 ---------------------------------- */

/* RECEIVE DIAGNOSTIC RESULTS returns six bytes with an additional length of
 * `0004h`, §13.2. */
#define AP_EXB8200_DIAGNOSTIC_BYTES 6u
#define AP_EXB8200_DIAGNOSTIC_ADDITIONAL 0x0004u
#define AP_EXB8200_DIAG_ROVFL 0x02u
#define AP_EXB8200_DIAG_TOVFL 0x01u
/* §18.2: "the EXB-8200 supports the following Diagnostic Option only". */
#define AP_EXB8200_DIAG_OPTION_COUNTERS 0x0001u
/* §18.3's five legal SelfTest/DevOfL/UnitOfL combinations, as the low three
 * bits of CDB byte 01. */
#define AP_EXB8200_TEST_COUNTERS 0x0u
#define AP_EXB8200_TEST_POWER_ON 0x4u
#define AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_NO_TAPE 0x5u
#define AP_EXB8200_TEST_POWER_ON_TAPE 0x6u
#define AP_EXB8200_TEST_POWER_ON_FUNCTIONAL_TAPE 0x7u

/* ---- Timing, `[EXBPS]` ch. 3 and `[EXB]` §23.2 --------------------------- */

#define AP_EXB8200_US(n) ((ap_time_t)(AP_TIME_BASE_HZ / 1000000u) * (n))

/* §3.1 and §3.2: "the maximum time from the last byte of the CDB to the first
 * data byte REQ". Both are maxima, so both are modelled at their stated value
 * -- a bound taken as the value, `CLAUDE.md`'s rule for a published range. */
#define AP_EXB8200_T_WRITE_ACCESS AP_EXB8200_US(950)
#define AP_EXB8200_T_READ_ACCESS AP_EXB8200_US(900)
/* §3.4: "1,082 to 1,115 ms". A **range**, so the point value charged here is
 * `PROVISIONAL` -- the midpoint would be an invention and the manual gives no
 * typical, so the minimum is charged and named. */
#define AP_EXB8200_T_REPOSITION AP_EXB8200_US(1082000)
/* §3.5: 33.3 ms, 1800 RPM. */
#define AP_EXB8200_T_DRUM AP_EXB8200_US(33300)
/* §23.2 and `[EXBPS]` §10.1.2: the drive "cannot respond to any SCSI bus
 * signals for a minimum of 300 milliseconds" after any reset. The *bus* holds
 * this, `AP_SCSI_T_RESET_SILENCE`; it is repeated here because §23.2 adds a
 * second interval the bus does not know about. */
#define AP_EXB8200_T_TNP_VALID ((ap_time_t)AP_TIME_BASE_HZ * 5u)

/* ---- The medium ---------------------------------------------------------- */

typedef enum {
  AP_EXB8200_RECORD_DATA = 0,
  AP_EXB8200_RECORD_FILEMARK,
} ap_exb8200_record_kind_t;

typedef struct {
  ap_exb8200_record_kind_t kind;
  uint32_t offset; /* into the data area, for a data block */
  uint32_t length; /* bytes, for a data block */
  bool short_mark; /* §22.1 byte 05 bit 7, for a filemark */
} ap_exb8200_record_t;

/* Caller-owned storage, the same shape `ap_ct_t` has and for the same reason: a
 * medium is the caller's, and a drive borrows it. `records` is how many are
 * live and `capacity` how many the array can hold, so a write that would run
 * off the end is PEOT rather than a corruption. */
typedef struct {
  ap_exb8200_record_t *record;
  unsigned records;
  unsigned capacity;
  uint8_t *data;
  uint32_t data_bytes;
  uint32_t data_used;
  bool writable;
  /* §9.2's Medium Type, `81h`-`85h` for P6 sizes and `C1h`-`C4h` for P5. Held
   * rather than derived because autosizing is a measurement of a physical tape
   * this model does not have. */
  uint8_t medium_type;
  /* §9.3: the block count MODE SENSE returns is "the number of physical blocks
   * of data that can be written from LBOT to LEOT plus the number of blocks
   * occupied by LBOT (`500h`)". */
  uint32_t blocks_to_leot;
} ap_exb8200_media_t;

/* `[EXB]` §9.3's constant: the blocks LBOT itself occupies. */
#define AP_EXB8200_LBOT_BLOCKS 0x500u
/* §23.5 and `[EXBPS]` §4.3.1: the physical block. */
#define AP_EXB8200_PHYSICAL_BLOCK 1024u
/* §22: a long filemark occupies 270 tracks and a short one 60, and a track is
 * eight physical blocks -- `[EXBPS]` §4.3.3. */
#define AP_EXB8200_TRACK_BLOCKS 8u
#define AP_EXB8200_FILEMARK_LONG_TRACKS 270u
#define AP_EXB8200_FILEMARK_SHORT_TRACKS 60u

/* ---- Where the head is --------------------------------------------------- */

/* Ch. 24 is twelve sections of "after X, command Y does Z", and all of it falls
 * out of the terminations in chapters 11, 19, 21 and 22 plus **one fact those
 * chapters leave implicit**: after a write or a write-filemarks, a READ or a
 * forward SPACE block is *Illegal Request*, not Blank Check, because the head
 * is inside the gap track rather than at end of data. That is the only reason
 * this is an enum and not an index. */
typedef enum {
  AP_EXB8200_AT_LBOT = 0,   /* a load or a rewind left it here */
  AP_EXB8200_IN_DATA,       /* between records, readable */
  AP_EXB8200_AFTER_WRITE,   /* inside the gap track a write left */
  AP_EXB8200_AT_PEOT,       /* the physical end */
} ap_exb8200_where_t;

typedef struct {
  ap_exb8200_media_t media;
  bool loaded;   /* the tape is threaded into the path */
  bool present;  /* a cartridge is in the drive, loaded or not */
  bool prevented; /* PREVENT MEDIUM REMOVAL is in force */

  ap_exb8200_where_t where;
  unsigned at; /* index of the next record, when `where` is not AT_PEOT */

  /* Mode parameters as MODE SELECT last set them, §8.4's defaults at reset. */
  bool buffered;
  uint32_t block_length; /* 0 means variable */
  uint8_t vendor_flags;  /* vendor byte 00 */
  bool p5;
  uint8_t motion_threshold;
  uint8_t reconnect_threshold;
  uint8_t gap_threshold;

  /* §16: a reservation belongs to an initiator, and the only thing this model
   * can tell initiators apart by is the SCSI ID the bus selected with. */
  bool reserved;
  uint8_t reserved_by;

  /* The sense data as the last Check Condition left it, §15.2. Held whole
   * because §15.3 says it survives a REQUEST SENSE and an INQUIRY and is
   * cleared by anything else. */
  uint8_t sense[AP_EXB8200_SENSE_BYTES];
  bool sense_valid;
  /* Whether a Unit Attention condition is still *pending*, which is not the
   * same as the sense buffer holding one. Table 15-1: the drive "clears the
   * Unit Attention sense key after it receives the next command from the
   * initiator" -- so the condition is reported exactly once, while the sense
   * it built stays for the REQUEST SENSE that follows. Without the two being
   * separate, every command after a reset reports Unit Attention forever. */
  bool unit_attention;

  /* §13.2's three counters, and §15.2's fourth. */
  uint8_t tracking_counter;
  uint8_t reread_counter;
  uint8_t write_recovery_counter;
  uint32_t error_counter;
  bool diagnostic_ready; /* a SEND DIAGNOSTIC has run */

  /* Counters for the boot report. */
  uint32_t commands;
  uint32_t check_conditions;
} ap_exb8200_t;

/* Power-on. §23.3: the first command after a power-on reset other than REQUEST
 * SENSE or INQUIRY is Unit Attention, which this arms. */
void ap_exb8200_power_on(ap_exb8200_t *drive);

/* §3.4's Bus Device Reset and a bus RST condition: "aborts all operations and
 * is re-initialized to the default state". Keeps the medium; a reset does not
 * eject. */
void ap_exb8200_reset(ap_exb8200_t *drive);

/* Put a cartridge in. `autoload` is §8.4's NAL bit seen from the outside: with
 * autoloading enabled -- the power-on default -- closing the door loads the
 * tape and positions it at LBOT. */
bool ap_exb8200_insert(ap_exb8200_t *drive, const ap_exb8200_media_t *media);

/* The front-panel button, §10.1.1. §20.2: an unload done this way leaves the
 * drive answering **Unit Attention** with TNP, where a LOAD/UNLOAD command
 * leaves it Not Ready -- a different key for the same physical state, and the
 * kind of distinction only the part's own manual carries. */
void ap_exb8200_unload_button(ap_exb8200_t *drive);

/* The target interface `ap_scsi_attach` wants. */
[[nodiscard]] ap_scsi_target_t ap_exb8200_target(ap_exb8200_t *drive);

/* How much of the medium is used, in physical blocks, counting a filemark's
 * tracks and a short logical block's gap bytes. §23.5 and §23.7. */
[[nodiscard]] uint32_t ap_exb8200_physical_blocks(const ap_exb8200_t *drive);

#endif /* APOLLO_DEVICE_AP_EXB8200_H */
