/* The SCSI bus, between the WD7000-ASC and its targets.
 *
 * `[WD7000]` *WD7000-ASC Engineering Specification* 96-000494 Rev. X3 for the
 * initiator's half, `[EXB]` *EXB-8200 User's Manual* 510006-007 and `[EXBPS]`
 * *EXB-8200 Product Specification* 510005-006 for the target's. All three are
 * walked whole -- `docs/references/WD7000_WALK.md` 140/140 and
 * `docs/references/EXB8200_WALK.md` 215/215.
 *
 * ## Why this is a transaction and not a set of wires
 *
 * `[WD7000]` §3: the ASC "contains an onboard **Z80** CPU", and the SBIC "does
 * arbitration, selection, disconnection and reselection" under that Z80's
 * firmware. The host never sees a bus phase: it writes a 32-byte SCB into
 * memory, rings a mailbox, and is interrupted with a completion code. So the
 * boundary this core has to be exact about is the **SCB and its ICMB**, and
 * the phases below it are the ASC's private business.
 *
 * Modelling REQ/ACK here would therefore be inventing an interface no
 * software on this machine can observe -- the same reason `ap_kbd` models the
 * keyboard-to-CPU packet and not the mouse's quadrature clocks, and the same
 * reason `ap_qic` stops at `QIC-02` §3.6's handshake. What *is* observable is
 * every byte of the CDB, every byte of the data, the SCSI status byte (SCB 14,
 * "unmodified by the ASC") and the vendor-unique error code (SCB 15). This
 * interface carries exactly those.
 *
 * **What that costs, named rather than hidden.** A disconnect-reconnect is not
 * modelled as a bus event: a target that would disconnect simply takes longer.
 * Nothing in the SCB records that it happened -- `[WD7000]` §5.7.1 posts the
 * same ICMB either way -- so no host-visible value differs. Recorded as a
 * deliberate approximation in `docs/PROJECT_STATUS.md`, with the discriminator
 * that would reopen it: a driver that times a command finely enough to see the
 * gap, which `scsi14.drvr` does not.
 *
 * ## Addressing
 *
 * Eight IDs, and eight LUNs per ID. `[EXB]` §3.1 and §4.3 both say the
 * EXB-8200's "LUN is always 0" and `[EXBPS]` §9.3 that it is "hard wired as
 * LUN 0", but the *bus* has three LUN bits because `[WD7000]` SCB byte 01 puts
 * a target ID in bits 7-5 and a LUN in bits 2-0, and a target that refuses a
 * non-zero LUN has to be given one to refuse.
 */

#ifndef APOLLO_DEVICE_AP_SCSI_H
#define APOLLO_DEVICE_AP_SCSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "time/ap_time.h"

#define AP_SCSI_IDS 8u
#define AP_SCSI_LUNS 8u

/* `[WD7000]` SCB bytes `02`-`13`: "the 12-byte SCSI CDB", written straight to
 * the SBIC. `[EXB]` §4.2 uses only the Group 0 six-byte form, and Table 6-6
 * accepts 6 to 12 with 1 to 5 "outside ANSI" -- so the bus carries up to
 * twelve and the target decides what it will take. */
#define AP_SCSI_CDB_MAX 12u

/* The group code is the CDB's top three bits, `[EXB]` §4.3. Group 0 is six
 * bytes; the rest are named so a target can refuse them by name. */
#define AP_SCSI_GROUP_MASK 0xE0u
#define AP_SCSI_GROUP_SHIFT 5u
#define AP_SCSI_GROUP_0 0u
#define AP_SCSI_CDB_GROUP_0 6u

/* Status byte codes, `[EXB]` Table 4-2. Bit 0 is always zero and bits 7-5 are
 * reserved, so these four values are the whole set this bus can carry. */
#define AP_SCSI_STATUS_GOOD 0x00u
#define AP_SCSI_STATUS_CHECK_CONDITION 0x02u
#define AP_SCSI_STATUS_BUSY 0x08u
#define AP_SCSI_STATUS_RESERVATION_CONFLICT 0x18u

/* The eleven messages `[EXB]` Table 3-1 supports, and no others: the drive
 * "does not support the extended message format or the use of linked
 * commands". Carried as definitions rather than as a modelled phase, for the
 * reason in the header comment -- the host cannot see a message go by, but a
 * target's *behaviour* on receiving one is host-visible, and `ap_scsi_message`
 * is how a bus reset or an abort reaches a target. */
#define AP_SCSI_MSG_COMMAND_COMPLETE 0x00u
#define AP_SCSI_MSG_SAVE_DATA_POINTER 0x02u
#define AP_SCSI_MSG_RESTORE_POINTERS 0x03u
#define AP_SCSI_MSG_DISCONNECT 0x04u
#define AP_SCSI_MSG_INITIATOR_DETECTED_ERROR 0x05u
#define AP_SCSI_MSG_ABORT 0x06u
#define AP_SCSI_MSG_REJECT 0x07u
#define AP_SCSI_MSG_NO_OPERATION 0x08u
#define AP_SCSI_MSG_PARITY_ERROR 0x09u
#define AP_SCSI_MSG_BUS_DEVICE_RESET 0x0Cu
/* `[EXB]` §3.1: `80h` identifies, and bit 6 grants disconnect privilege, so
 * `C0h` is the same message with the privilege. Bits 2-0 are the LUN. */
#define AP_SCSI_MSG_IDENTIFY 0x80u
#define AP_SCSI_MSG_IDENTIFY_DISCPRIV 0xC0u
#define AP_SCSI_IDENTIFY_LUN_MASK 0x07u

/* Which way the data phase runs, from the *initiator's* point of view -- which
 * is the direction `[WD7000]` SCB byte 25 bit 7 encodes: "set when data is
 * written to the host, so every SCSI read sets it and every write clears it". */
typedef enum {
  AP_SCSI_DATA_NONE = 0,
  AP_SCSI_DATA_IN,  /* target to initiator; SCB 25 bit 7 set */
  AP_SCSI_DATA_OUT, /* initiator to target; SCB 25 bit 7 clear */
} ap_scsi_dir_t;

/* What a transaction produced. A target fills this in; the ASC copies `status`
 * into SCB byte 14 unmodified (`[WD7000]` §5.4.1) and decides the ICMB code
 * from it. */
typedef struct {
  uint8_t status;        /* one of the four `AP_SCSI_STATUS_*` */
  ap_scsi_dir_t direction;
  unsigned transferred;  /* bytes actually moved */
  /* `[WD7000]` §A.7 vue `40`: "target sent less than the allocation length" is
   * a *warning*, not a failure, so short is reported rather than refused. */
  bool short_transfer;
  /* How long the target says the command took, in `AP_TIME_BASE_HZ` units.
   * Zero is legal and means "no motion" -- `[EXB]` ch. 11 and 19 both make a
   * zero transfer length a no-op that is "not an error". */
  ap_time_t duration;
} ap_scsi_result_t;

/* One target's behaviour. Function pointers rather than an enum switch,
 * following `ap_3c505.h`'s `transmit` hook: the bus must not know what kinds
 * of target exist, or adding the second one edits the first one's file. */
typedef struct {
  void *device; /* the target's own state, e.g. an `ap_exb8200_t *` */

  /* Execute one CDB. `data` is the initiator's buffer: for `AP_SCSI_DATA_OUT`
   * it already holds `capacity` bytes to be consumed, for `AP_SCSI_DATA_IN` it
   * is filled. Returns false only when the target cannot be selected at all --
   * a refusal *is* a transaction, and comes back as Check Condition. */
  bool (*execute)(void *device, uint8_t lun, const uint8_t *cdb,
                  unsigned cdb_length, uint8_t *data, unsigned capacity,
                  ap_scsi_result_t *result);

  /* `[EXB]` §3.4: a Bus Device Reset "aborts all operations and re-initialises
   * to the default state", and a bus RST condition does the same. One entry
   * point for both, because the drive treats them identically -- §23.2 gives
   * both the same 300 ms of silence. */
  void (*reset)(void *device);

  /* Whether the target answers selection at all. A drive whose power is off is
   * not a Busy status, it is a selection timeout. */
  bool (*present)(const void *device);
} ap_scsi_target_t;

/* `[WD7000]` §6.2.11.4: the SCSI timeout is `timeout(ms) x SBIC clock(MHz) /
 * 80`, default `19h` = 25 decimal = **250 ms**, and vue `4D` is
 * "selection/reselection timeout". An empty address is not an error in the
 * bus's own terms -- it is a command that takes 250 ms and comes back `4D`. */
#define AP_SCSI_T_SELECTION_TIMEOUT ((ap_time_t)AP_TIME_BASE_HZ / 4u)

/* `[EXB]` §23.2 and `[EXBPS]` §10.1.2: after a power-on reset, a SCSI bus
 * reset or a Bus Device Reset the drive "cannot respond to any SCSI bus
 * signals for a minimum of 300 milliseconds". The bus holds this, not the
 * target, because it is the interval during which *selection* fails. */
#define AP_SCSI_T_RESET_SILENCE ((ap_time_t)AP_TIME_BASE_HZ * 3u / 10u)

typedef struct {
  ap_scsi_target_t target[AP_SCSI_IDS];
  bool fitted[AP_SCSI_IDS];
  /* The initiator's own ID, `[WD7000]` §6.1.2 byte 01, three bits. A host that
   * selects itself is selecting nothing. */
  uint8_t initiator_id;
  /* Set while a reset's silence is running; selection fails until it ends. */
  ap_time_t quiet_until;
  ap_time_t now;
  /* Counters, for the boot report -- the same shape `ap_sc499` reports. */
  uint32_t selections;
  uint32_t selection_timeouts;
  uint32_t commands;
  uint32_t check_conditions;
} ap_scsi_bus_t;

/* Power-on: every address empty, no initiator, no silence outstanding. */
void ap_scsi_bus_init(ap_scsi_bus_t *bus, uint8_t initiator_id);

/* Put a target at `id`. Replacing one is legal -- that is a cable change --
 * and putting one at the initiator's own ID is refused, because `[WD7000]`
 * Table 6-5 calls the local host's slot "a placeholder". */
bool ap_scsi_attach(ap_scsi_bus_t *bus, uint8_t id,
                    const ap_scsi_target_t *target);

/* Whether an address would answer selection *now*: fitted, present, and past
 * the reset silence. */
[[nodiscard]] bool ap_scsi_selectable(const ap_scsi_bus_t *bus, uint8_t id);

/* Advance the bus's clock. Only the reset silence lives here; targets keep
 * their own time. */
void ap_scsi_advance(ap_scsi_bus_t *bus, ap_time_t now);

/* A SCSI bus RST, from `[WD7000]` Host Control bit 1 or from a target. Resets
 * every attached target and starts the 300 ms silence. */
void ap_scsi_reset(ap_scsi_bus_t *bus);

/* Send one of Table 3-1's out-messages to a target. Only `ABORT` and
 * `BUS DEVICE RESET` change anything at this level of modelling; the rest are
 * accepted and counted, which is what "handled by the LCPU transparently"
 * (`[WD7000]` §5.6.1) looks like from here. */
bool ap_scsi_message(ap_scsi_bus_t *bus, uint8_t id, uint8_t lun,
                     uint8_t message);

/* Run one command. Returns false when nothing answered -- the caller charges
 * `AP_SCSI_T_SELECTION_TIMEOUT` and posts vue `4D`. Otherwise `result` is
 * filled and true is returned, *including* for a Check Condition: a target
 * that refuses a command has still been selected. */
bool ap_scsi_command(ap_scsi_bus_t *bus, uint8_t id, uint8_t lun,
                     const uint8_t *cdb, unsigned cdb_length, uint8_t *data,
                     unsigned capacity, ap_scsi_result_t *result);

/* The CDB length a group code implies, or 0 for a group this bus does not
 * carry. `[EXB]` §4.3: "The EXB-8200 supports Group 0 commands only. All
 * commands must contain 0 in this field; any other value results in an error
 * condition." */
[[nodiscard]] unsigned ap_scsi_cdb_length(uint8_t opcode);

#endif /* APOLLO_DEVICE_AP_SCSI_H */
