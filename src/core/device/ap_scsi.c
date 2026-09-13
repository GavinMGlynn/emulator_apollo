#include "device/ap_scsi.h"

#include <string.h>

void ap_scsi_bus_init(ap_scsi_bus_t *bus, uint8_t initiator_id) {
  if (bus == nullptr) {
    return;
  }
  memset(bus, 0, sizeof *bus);
  /* Three bits, `[WD7000]` §6.1.2 byte 01. A wider value would silently alias
   * onto a real address, which is the one way an initiator can shadow a
   * target it is supposed to be talking to. */
  bus->initiator_id = (uint8_t)(initiator_id & (AP_SCSI_IDS - 1u));
}

bool ap_scsi_attach(ap_scsi_bus_t *bus, uint8_t id,
                    const ap_scsi_target_t *target) {
  if (bus == nullptr || target == nullptr || id >= AP_SCSI_IDS) {
    return false;
  }
  /* `[WD7000]` Table 6-5 calls the initiator's own slot "a placeholder": there
   * are eight buffers, one per SCSI ID, and the local host's is not a device.
   * Refusing here is what stops a target answering its own initiator. */
  if (id == bus->initiator_id) {
    return false;
  }
  if (target->execute == nullptr || target->present == nullptr) {
    return false;
  }
  bus->target[id] = *target;
  bus->fitted[id] = true;
  return true;
}

bool ap_scsi_selectable(const ap_scsi_bus_t *bus, uint8_t id) {
  if (bus == nullptr || id >= AP_SCSI_IDS || !bus->fitted[id]) {
    return false;
  }
  /* `[EXB]` §23.2: after any reset the drive "cannot respond to any SCSI bus
   * signals for a minimum of 300 milliseconds". During that window a
   * selection does not fail *fast* -- it times out like an empty address,
   * which is why this is one test and not two. */
  if (bus->now < bus->quiet_until) {
    return false;
  }
  return bus->target[id].present(bus->target[id].device);
}

void ap_scsi_advance(ap_scsi_bus_t *bus, ap_time_t now) {
  if (bus == nullptr || now < bus->now) {
    return;
  }
  bus->now = now;
}

void ap_scsi_reset(ap_scsi_bus_t *bus) {
  if (bus == nullptr) {
    return;
  }
  for (unsigned id = 0; id < AP_SCSI_IDS; id++) {
    if (bus->fitted[id] && bus->target[id].reset != nullptr) {
      bus->target[id].reset(bus->target[id].device);
    }
  }
  /* The silence is dated from *now*, so a second reset inside the window
   * restarts it rather than shortening it -- which is what a drive that
   * re-runs its power-on sequence does. */
  bus->quiet_until = bus->now + AP_SCSI_T_RESET_SILENCE;
}

bool ap_scsi_message(ap_scsi_bus_t *bus, uint8_t id, uint8_t lun,
                     uint8_t message) {
  if (bus == nullptr || id >= AP_SCSI_IDS || lun >= AP_SCSI_LUNS) {
    return false;
  }
  if (!ap_scsi_selectable(bus, id)) {
    return false;
  }
  switch (message) {
  case AP_SCSI_MSG_BUS_DEVICE_RESET:
    /* `[EXB]` §3.4: "aborts all operations and is re-initialized to the
     * default state". §23.2 gives a re-initialised drive the same 300 ms of
     * silence as a power-on reset, so this target goes quiet with it -- but
     * the *bus* does not, because a Bus Device Reset is addressed to one
     * device where a RST condition is not. */
    if (bus->target[id].reset != nullptr) {
      bus->target[id].reset(bus->target[id].device);
    }
    bus->quiet_until = bus->now + AP_SCSI_T_RESET_SILENCE;
    return true;
  case AP_SCSI_MSG_ABORT:
    /* `[EXB]` §3.4: for READ and WRITE the effect is to terminate; "for all
     * other commands, the operation is completed and the SCSI bus is
     * released". This core runs a command to completion inside
     * `ap_scsi_command`, so there is never a command in flight for an Abort
     * to catch -- the message is accepted and does nothing, which is exactly
     * the documented behaviour for the "all other commands" case and is a
     * deliberate approximation for READ and WRITE. Named in
     * `docs/PROJECT_STATUS.md` with its discriminator: a host that aborts a
     * transfer it has not finished feeding, which needs the disconnect this
     * bus does not model. */
    return true;
  case AP_SCSI_MSG_COMMAND_COMPLETE:
  case AP_SCSI_MSG_SAVE_DATA_POINTER:
  case AP_SCSI_MSG_RESTORE_POINTERS:
  case AP_SCSI_MSG_DISCONNECT:
  case AP_SCSI_MSG_INITIATOR_DETECTED_ERROR:
  case AP_SCSI_MSG_REJECT:
  case AP_SCSI_MSG_NO_OPERATION:
  case AP_SCSI_MSG_PARITY_ERROR:
    /* `[WD7000]` §5.6.1: "most messages are handled by the LCPU
     * transparently". Accepted and counted as nothing, which is what
     * transparent means from above the ASC. */
    return true;
  default:
    break;
  }
  /* An Identify is the one remaining shape, and it is a *field*, not a value:
   * bit 7 set, bit 6 the disconnect privilege, bits 2-0 the LUN. */
  if ((message & AP_SCSI_MSG_IDENTIFY) != 0u) {
    return (message & AP_SCSI_IDENTIFY_LUN_MASK) == lun;
  }
  /* `[EXB]` Table 3-1 lists eleven messages and the note under it says the
   * extended format is not supported, so anything else is rejected rather
   * than ignored -- §3.1's Message Reject, which the initiator sees as the
   * message not having been taken. */
  return false;
}

unsigned ap_scsi_cdb_length(uint8_t opcode) {
  const unsigned group = (unsigned)((opcode & AP_SCSI_GROUP_MASK) >>
                                    AP_SCSI_GROUP_SHIFT);
  /* `[EXB]` §4.3: "The EXB-8200 supports Group 0 commands only. All commands
   * must contain 0 in this field; any other value results in an error
   * condition." The bus refuses the rest by returning no length, so a target
   * never has to decide how many bytes a command it cannot run would have
   * taken. */
  return group == AP_SCSI_GROUP_0 ? AP_SCSI_CDB_GROUP_0 : 0u;
}

bool ap_scsi_command(ap_scsi_bus_t *bus, uint8_t id, uint8_t lun,
                     const uint8_t *cdb, unsigned cdb_length, uint8_t *data,
                     unsigned capacity, ap_scsi_result_t *result) {
  if (bus == nullptr || result == nullptr || cdb == nullptr) {
    return false;
  }
  if (id >= AP_SCSI_IDS || lun >= AP_SCSI_LUNS) {
    return false;
  }
  /* `[WD7000]` SCB bytes `02`-`13` are twelve, and `[EXB]` Table 6-6 accepts
   * six to twelve with one to five "outside ANSI". A zero-length CDB is not a
   * command at all. */
  if (cdb_length == 0u || cdb_length > AP_SCSI_CDB_MAX) {
    return false;
  }
  bus->selections++;
  if (!ap_scsi_selectable(bus, id)) {
    /* The caller charges `AP_SCSI_T_SELECTION_TIMEOUT` and posts vue `4D`.
     * Counted here rather than there because the bus is what knows the
     * address was empty. */
    bus->selection_timeouts++;
    return false;
  }

  memset(result, 0, sizeof *result);
  result->status = AP_SCSI_STATUS_GOOD;
  result->direction = AP_SCSI_DATA_NONE;

  if (!bus->target[id].execute(bus->target[id].device, lun, cdb, cdb_length,
                               data, capacity, result)) {
    /* A target that answered selection and then could not run at all is a
     * hardware failure, not a timeout: it has been selected, so the
     * transaction exists and carries Check Condition. */
    result->status = AP_SCSI_STATUS_CHECK_CONDITION;
    result->direction = AP_SCSI_DATA_NONE;
    result->transferred = 0u;
  }
  bus->commands++;
  if (result->status == AP_SCSI_STATUS_CHECK_CONDITION) {
    bus->check_conditions++;
  }
  /* A target may not claim to have moved more than the initiator offered.
   * `[WD7000]` SCB bytes 16-18 are "maximum data transfer length, the
   * runaway-target guard", and this is where that guard lives. */
  if (result->transferred > capacity) {
    result->transferred = capacity;
  }
  return true;
}
