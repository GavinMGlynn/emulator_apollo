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
  /* A reset abandons whatever the drive was working towards. Cleared rather
   * than left standing because the deadline is hashed: two idle controllers
   * that reached idle by different routes are the same machine and must hash
   * alike. */
  omti->completion_at = 0u;

  /* And the identification block, because §5.4.13's "after a RESET is done
   * (before any other command)" is a statement about the *buffer*: the reset
   * writes it and the next command overwrites it. Modelling it as a flag would
   * say the same thing less directly and would have to be cleared in as many
   * places as there are commands. See the header for the layout and for what
   * each field is sourced from. */
  memset(omti->buffer, 0, sizeof omti->buffer);
  memcpy(omti->buffer, AP_OMTI_IDENTIFICATION, AP_OMTI_IDENTIFICATION_BYTES);
  /* The two checksum bytes stay zero: no source here gives the ROM's checksum,
   * and the oracle's `xx` is a placeholder rather than a value. The four error
   * bytes stay zero because this controller has just passed its own power-on
   * tests -- which is the whole of what the boot PROM checks. */
  omti->buffer[AP_OMTI_ID_BUFFER_SIZE] = AP_OMTI_ID_BUFFER_32K;
}

void ap_omti_reset(ap_omti_t *omti) {
  memset(omti, 0, sizeof *omti);
  ap_omti_disk_reset(omti);

  /* And the measured floppy half: `00` main status and `80` digital input, the
   * latter being the diskette-change bit with no media in the drive. */
  omti->fdc_data = 0xFFu;
  omti->disk_change = true;
  omti->fdc_completion_at = 0u;
  /* Not zero: no drive is seeking, and zero is a deadline a freshly started
   * machine has already passed. */
  omti->fdc_seek_at[0] = AP_TIME_NEVER;
  omti->fdc_seek_at[1] = AP_TIME_NEVER;
}

bool ap_omti_disk_dma_request(const ap_omti_t *omti) {
  /* The bit alone: the controller sets it only when DMA is enabled *and* a data
   * phase is live, and clears it when the phase ends, so both halves of the
   * condition are already in it. */
  return (omti->status & AP_OMTI_ST_DREQ) != 0u;
}

ap_time_t ap_omti_interrupt_next_change(const ap_omti_t *omti) {
  /* The guards are `ap_omti_advance`'s own, so the two cannot disagree about
   * when this part is capable of moving. Three deadlines, because the two
   * halves of this board are independent and the floppy's drives are
   * independent of each other: the soonest of them is when anything here can
   * next change. */
  ap_time_t next = AP_TIME_NEVER;
  if (omti->phase == AP_OMTI_PHASE_EXECUTING && omti->completion_at < next) {
    next = omti->completion_at;
  }
  if (omti->fdc_phase == AP_OMTI_PHASE_EXECUTING &&
      omti->fdc_completion_at < next) {
    next = omti->fdc_completion_at;
  }
  for (unsigned unit = 0u; unit < 2u; unit++) {
    if (omti->fdc_seek_at[unit] < next) {
      next = omti->fdc_seek_at[unit];
    }
  }
  return next;
}

bool ap_omti_disk_irq(const ap_omti_t *omti) {
  return (omti->mask & AP_OMTI_MASK_INTERRUPT_ENABLE) != 0u &&
         (omti->status & AP_OMTI_ST_IREQ) != 0u;
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
/* §5.3's status register, bit 5. */
#define COMPLETION_LUN 0x20u
/* §5.3's third field, and the one this file named only in passing: bits 3 and 2
 * are the **Error Recovery Status**, "valid only for commands which read data
 * from the disk".
 *
 *     0 0  No error recovery
 *     0 1  One retry accomplished successfully
 *     1 0  More than one retry accomplished successfully
 *     1 1  Error correction done successfully
 *
 * Always `00` here, and that is a fact about this model rather than an omission:
 * an `.awd` image is sector data with no medium under it, a read either
 * addresses a sector the image holds or is refused, so no read has ever needed a
 * retry or an ECC correction to succeed. It becomes observable on the same day
 * `AP_OMTI_CONTROL_DISABLE_RETRY` and `DISABLE_ECC` do -- when media errors can
 * be injected -- and the field is named now so that the day it does, the value
 * has somewhere to go.
 *
 * `002398-04` p. 12-10 draws this byte too, and draws bits 3 and 2 as zero; its
 * one addition is a legend for **bit 6** -- "1 => winchester status, 0 => tape
 * status" -- which §5.3 gives as one of the four bits "set to zero". Not
 * modelled: the tape on this machine is an SC-499 at its own address and does
 * not answer through this controller's data port, so a bit distinguishing the
 * two would report a device this part cannot see. Recorded rather than dropped,
 * because the handbook's page is the only place it appears. */
#define COMPLETION_ERROR_RECOVERY_MASK 0x0Cu
#define COMPLETION_ERROR_RECOVERY_NONE 0x00u
#define COMPLETION_ERROR_RECOVERY_ONE_RETRY 0x04u
#define COMPLETION_ERROR_RECOVERY_RETRIES 0x08u
#define COMPLETION_ERROR_RECOVERY_CORRECTED 0x0Cu

/* §5.1.3's sense bytes, named by Appendix A, "Sense Code Summary and
 * Description". Only the codes this core can genuinely produce are ever set;
 * everything else would be inventing a failure mode.
 *
 * `20` and `21` are one line apart in the appendix and say entirely different
 * things, which is the distinction this model spent two boots not making:
 *
 *   20 Invalid Command. "the controller decoded a command code that it does
 *      not support."
 *   21 Illegal Disk Address. "a command with a Sector Address beyond the
 *      capacity of the drive. Check the number of cylinders, heads and sector
 *      size that the drive is configured for."
 *
 * A command this core has not modelled is the first of those and not the
 * second. Reporting it as `21` told Domain/OS its *geometry* was wrong, which
 * is a lie about a part of the system that was working, and sent it down a path
 * that ends in a fatal status several layers from the actual cause. */
#define SENSE_INVALID_COMMAND 0x20u
#define SENSE_ILLEGAL_ADDRESS 0x21u
#define SENSE_DRIVE_NOT_READY 0x04u
/* `23 Volume Overflow`, and it is one line below `21` in the same appendix:
 * "This indicates that **after the commencement of a multiblock command**, the
 * end of volume was reached."
 *
 * So the two codes divide the same failure by *when* it happens. `21` is the
 * address the command named being outside the drive; `23` is a command whose
 * address was inside it running off the end part-way through. Every multiblock
 * path here reported `21` for both, which tells a driver its descriptor block
 * was wrong when the descriptor block was accepted and the transfer had already
 * started -- the same class of misreport as `20` against `21` two paragraphs
 * above, and found the same way, by walking the table rather than by a failure.
 *
 * `002398-04` p. 12-11 is what sent this file back to the appendix: its error
 * code list prints `Volume Overflow $23` beside the four codes this core does
 * emit, and there was no path here that could ever produce it.
 *
 * That page's list agrees with Appendix A on every code both print, and adds two
 * the appendix does not have -- `32 Processor Test error` and `33 Winc control
 * test error`, where the appendix's TYPE 3 stops at `31`. Both are results of
 * the controller's own self tests, which this model passes: §5.4.23 and §5.4.26
 * have "no fault to report", so a code for a failure that cannot happen would be
 * inventing one. Recorded because the page is the only place they appear, and
 * because Apollo shipped its own firmware in this controller -- the
 * `3000_OMTI_8621_102640-B` ROM -- which is where two extra diagnostic codes
 * would come from. */
#define SENSE_VOLUME_OVERFLOW 0x23u
/* `22 Illegal Function for Drive Type`, and Appendix A names the command in the
 * description itself: "a Change Cartridge command (HEX 1B) was issued to a LUN
 * assigned as a Fixed drive type". This machine's drive is fixed, so that is
 * `1B`'s whole behaviour here and there is nothing to guess. */
#define SENSE_ILLEGAL_FUNCTION 0x22u
/* `17 Write Protected`, Appendix A: "This indicates that during a WRITE/FORMAT
 * command, the controller detected a WRITE PROTECTED signal from the selected
 * Logical Unit Number."
 *
 * An image opened read-only *is* a write-protected drive, and this is the code
 * that says so. Without it a refused write reported `21 ILLEGAL DISK ADDRESS`,
 * which is the same lie the unimplemented-command arm used to tell: the address
 * was fine, and the host was sent to check a geometry that was correct. It cost
 * a boot to find, and the address it named -- cylinder 0, head 0, sector 1 --
 * was the giveaway, being the second sector of the disk. */
#define SENSE_WRITE_PROTECTED 0x17u
/* `19 Bad Track Encountered` and `1C Illegal Access To An Alternate Track`,
 * both Appendix A, and both unreachable until the sidecar gave the ID field
 * somewhere to live. */
#define SENSE_BAD_TRACK 0x19u
#define SENSE_ALTERNATE_DIRECT 0x1Cu

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

unsigned ap_omti_refusals(const ap_omti_t *omti) { return omti->refusals; }
uint16_t ap_omti_refused_cylinder(const ap_omti_t *omti) {
  return omti->refused_cylinder;
}
uint8_t ap_omti_refused_head(const ap_omti_t *omti) {
  return omti->refused_head;
}
uint8_t ap_omti_refused_sector(const ap_omti_t *omti) {
  return omti->refused_sector;
}
uint32_t ap_omti_refused_lba(const ap_omti_t *omti) {
  return omti->refused_lba;
}

const uint8_t *ap_omti_refused_cdb(const ap_omti_t *omti) {
  return omti->refused_cdb;
}

unsigned ap_omti_reads(const ap_omti_t *omti) { return omti->recent_read_count; }

bool ap_omti_recent_read(const ap_omti_t *omti, unsigned index, uint32_t *lba) {
  const unsigned kept = sizeof omti->recent_reads / sizeof omti->recent_reads[0];
  if (index >= kept || index >= omti->recent_read_count) {
    return false;
  }
  *lba = omti->recent_reads[(omti->recent_read_count - 1u - index) % kept];
  return true;
}

unsigned ap_omti_commands_recorded(const ap_omti_t *omti) {
  return omti->recent_command_count;
}

bool ap_omti_recent_command(const ap_omti_t *omti, unsigned index,
                            uint8_t *command, uint16_t *blocks, uint32_t *lba,
                            uint32_t *drained) {
  const unsigned kept =
      sizeof omti->recent_commands / sizeof omti->recent_commands[0];
  if (index >= kept || index >= omti->recent_command_count) {
    return false;
  }
  const unsigned at = (omti->recent_command_count - 1u - index) % kept;
  *command = omti->recent_commands[at].command;
  *blocks = omti->recent_commands[at].blocks;
  *lba = omti->recent_commands[at].lba;
  *drained = omti->recent_commands[at].drained;
  return true;
}

/* Record what was refused, so a run can say which address rather than only that
 * there was one. */
static void finish(ap_omti_t *omti, bool error, uint8_t sense);

/* Refuse an address, and **say which one**.
 *
 * §5.4.3 defines the sense block as more than its first byte: "the sector
 * address (defined by bytes 1, 2 and 3) is only valid if the previous command
 * terminated in error. **Bit 7 set to 1 indicates the validity of the sector
 * address.** If bit 7 is set to 0, the sector address is not valid." Bytes 1-3
 * carry it in exactly the layout a descriptor block's bytes 1-3 use.
 *
 * This core sent zeros with the flag clear, so a driver that asked where a read
 * failed was told the answer was not available -- from a controller that knew
 * it, and had already recorded it for its own report.
 *
 * Recording and reporting are one function because they were two, and every
 * caller had to remember to do both.
 *
 * `sense` is the caller's because the address is reported the same way for two
 * different codes: `21` for an address the command named and the drive does not
 * have, `23` for a multiblock command that ran off the end after it had
 * started. Both are addresses this controller knows, and §5.4.3's validity bit
 * says so for either. */
static void refuse(ap_omti_t *omti, uint16_t cylinder, uint8_t head,
                   uint8_t sector, uint32_t lba, uint8_t sense) {
  omti->refused_cylinder = cylinder;
  omti->refused_head = head;
  omti->refused_sector = sector;
  omti->refused_lba = lba;
  omti->refusals++;
  for (unsigned i = 0; i < sizeof omti->refused_cdb; i++) {
    omti->refused_cdb[i] = omti->command[i];
  }

  finish(omti, true, sense);
  omti->sense[0] = (uint8_t)(sense | AP_OMTI_SENSE_ADDRESS_VALID);
  omti->sense[1] = (uint8_t)((head & 0x1Fu) |
                             ((cylinder & 0x0400u) != 0u ? 0x80u : 0x00u));
  omti->sense[2] = (uint8_t)((sector & 0x3Fu) |
                             (uint8_t)(((cylinder >> 8) & 0x03u) << 6));
  omti->sense[3] = (uint8_t)(cylinder & 0xFFu);
}

/* Cylinder, head and sector from a linear sector number, for the one path that
 * has only the linear one. `ap_awd_lba` lets a descriptor address a sector past
 * its own track and carry into the next, so this is the inverse of the
 * *normalised* form -- which is where the access actually landed, and so what
 * failed. */
static void chs_of(ap_awd_geometry_t g, uint32_t lba, uint16_t *cylinder,
                   uint8_t *head, uint8_t *sector) {
  const uint32_t track = lba / g.sectors;
  *sector = (uint8_t)(lba % g.sectors);
  *head = (uint8_t)(track % g.heads);
  *cylinder = (uint16_t)(track / g.heads);
}

/* How long the drive takes over the command in `omti->command`.
 *
 * Zero for everything the controller answers out of its own registers -- those
 * never touch a surface, so there is nothing to wait for and pretending
 * otherwise would be inventing time. Non-zero for the commands that position
 * the heads, and built from the drive's published figures as separate
 * components so that changing the drive changes numbers rather than structure.
 * See the header for the `PROVISIONAL` marking on which drive this is. */
static ap_time_t command_duration(const ap_omti_t *omti) {
  if (!ap_omti_cdb_touches_surface(omti->command[0])) {
    return 0u;
  }
  /* The block count the command asked for, which is what crossed the surface.
   * Taken from the descriptor block rather than counted during the transfer,
   * because a command that refused its address moved nothing and still cost the
   * seek that discovered so. */
  ap_omti_cdb_t cdb = {0};
  ap_omti_cdb_decode(omti->command, &cdb);
  const uint64_t bytes = (uint64_t)cdb.block_count * AP_AWD_SECTOR_BYTES;
  const ap_time_t transfer = (ap_time_t)(
      (uint64_t)AP_TIME_BASE_HZ * bytes / AP_OMTI_TRANSFER_BYTES_PER_SEC);
  return AP_OMTI_AVERAGE_SEEK + AP_OMTI_AVERAGE_LATENCY + transfer;
}

/* Present the completion the drive has arrived at: the phase transition and the
 * status bits, and nothing that a caller may still be filling in.
 *
 * **The result bytes are deliberately not written here.** `refuse` calls
 * `finish` and then overwrites `sense` with the address that was refused, so a
 * completion that rewrote them on arrival would erase it -- which is exactly
 * what happened when the access time was first added, and what
 * `test_a_bad_address_fails_and_the_sense_says_so` caught. The bytes belong to
 * the moment the command *ended*; only their announcement is delayed. */
static void complete(ap_omti_t *omti) {
  omti->phase = AP_OMTI_PHASE_STATUS;
  omti->buffer_index = 0u;
  omti->blocks_left = 0u;
  /* §4.3's status state: "The controller sets the C/D bit and the I/O bit in
   * the STATUS byte", the byte waiting is a status byte and it travels *to* the
   * host.
   *
   * **`IREQ` is gated on the MASK register's interrupt enable**, and that is
   * the half of §4.2's ambiguous sentence this originally got wrong. It reads
   * "If the INTERRUPT ENABLE bit was previously set in the MASK register, the
   * REQ bit is set in the STATUS byte, along with IRQ14 on the system bus" --
   * taken literally, a polled driver would have no request to wait on at all,
   * so the reading taken here was that `REQ` is the phase's own handshake and
   * only the *interrupt* is gated. Half right: `REQ` is indeed ungated, and
   * `IREQ` is not.
   *
   * Domain/OS settles it. Its driver polls this register waiting for exactly
   * `CF` -- `BSY|C/D|I/O|REQ` with `IREQ` **clear** -- and with `IREQ` set
   * unconditionally the controller sat at `EF` for ever, which is the value the
   * operating system printed as `DISK CONTROLLER STATE = EF` before giving up.
   * `omti8621.cpp` agrees field for field: it sets `IREQ` inside
   * `if (m_mask_port & OMTI_MASK_INTE)` and nowhere else. */
  omti->status |= (uint8_t)(AP_OMTI_ST_CD | AP_OMTI_ST_IO | AP_OMTI_ST_REQ);
  if ((omti->mask & AP_OMTI_MASK_INTERRUPT_ENABLE) != 0u) {
    omti->status |= AP_OMTI_ST_IREQ;
  }
  /* And `DREQ` goes down, because the data phase it belonged to is over.
   *
   * `DREQ` is "1 = DMA Cycle Requested", and there is no DMA cycle in a status
   * phase: the status byte travels through the data register under `REQ`, not
   * under `DACK`. Left standing it was a request the 8237 would keep servicing
   * against a controller with nothing to give, which is a transfer that never
   * ends rather than one that ends wrong -- and it was invisible while nothing
   * connected the bit to a DMA channel at all. */
  omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_DREQ);
}

/* A command has reached its end. Whether that end is *now* depends on whether
 * the drive had to move: see `command_duration`, and the header for why a zero
 * access time is not merely imprecise but fatal to Domain/OS.
 *
 * While the deadline stands the controller is executing, so `REQ`, `C/D`, `I/O`
 * and `IREQ` are all down -- there is no byte to move and no completion to
 * report yet -- and `BSY` stays up, which is what a driver polling the status
 * register sees while it waits. */
static void finish(ap_omti_t *omti, bool error, uint8_t sense) {
  /* §5.3: bit 1 is the command status, and **bit 5 "indicates the LUN address
   * of the device associated with this command"**. Only bit 1 was ever set, so
   * a driver reading the completion byte was told every command belonged to
   * unit 0. Bits 7, 6, 4 and 0 are "set to zero", and the error-recovery field
   * at bits 3 and 2 is `COMPLETION_ERROR_RECOVERY_NONE` for the reason set out
   * where it is defined -- nothing in this model can retry, so no command has
   * ever recovered from anything.
   *
   * Written now, at the end of the command, rather than when the drive
   * announces it -- see `complete`. */
  omti->completion = (uint8_t)((error ? COMPLETION_ERROR : 0u) |
                               COMPLETION_ERROR_RECOVERY_NONE |
                               (omti->command_lun != 0u ? COMPLETION_LUN : 0u));
  omti->sense[0] = error ? sense : 0u;
  omti->sense[1] = 0u;
  omti->sense[2] = 0u;
  omti->sense[3] = 0u;

  const ap_time_t duration = command_duration(omti);
  if (duration == 0u) {
    complete(omti);
    return;
  }
  omti->phase = AP_OMTI_PHASE_EXECUTING;
  omti->completion_at = omti->now + duration;
  omti->status = (uint8_t)((omti->status &
                            ~(AP_OMTI_ST_IREQ | AP_OMTI_ST_DREQ | AP_OMTI_ST_CD |
                              AP_OMTI_ST_IO | AP_OMTI_ST_REQ)) |
                           AP_OMTI_ST_BSY);
}

/* Defined with the rest of the floppy half, far below: this is the only place
 * above it that needs to reach it. */
static void fdc_complete(ap_omti_t *omti);

void ap_omti_advance(ap_omti_t *omti, ap_time_t now) {
  omti->now = now;

  /* The heads first, then the controllers that may be waiting on them.
   *
   * A drive arriving at its cylinder is what `SENSE INTERRUPT STATUS` reports
   * and what takes the Main Status Register's per-drive Seek bit down. §6.3
   * gives `SEEK` and `RECALIBRATE` no result phase, so nothing else marks the
   * moment. */
  for (unsigned unit = 0u; unit < 2u; unit++) {
    if (omti->fdc_seek_at[unit] == AP_TIME_NEVER ||
        now < omti->fdc_seek_at[unit]) {
      continue;
    }
    omti->fdc_seek_at[unit] = AP_TIME_NEVER;
    omti->fdc_seek_done = true;
    omti->fdc_seek_st0 =
        (uint8_t)(AP_OMTI_ST0_IC_NORMAL | AP_OMTI_ST0_SEEK_END | (uint8_t)unit);
  }

  /* `>=` and not `>`: a deadline is the instant the completion is visible, and
   * a device advanced exactly onto it has reached it. */
  if (omti->fdc_phase == AP_OMTI_PHASE_EXECUTING &&
      now >= omti->fdc_completion_at) {
    fdc_complete(omti);
  }

  if (omti->phase != AP_OMTI_PHASE_EXECUTING) {
    return;
  }
  if (now >= omti->completion_at) {
    complete(omti);
  }
}

/* The linear block a CDB names, with no side effects and no refusal.
 *
 * Split out of `addressed` so the command log can record the *same* number the
 * command will act on. Recording `ap_awd_lba`'s answer instead would be right
 * only while `ENABLE SECTOR ADDRESS CONVERSION` is clear, and a log that is
 * right most of the time is the worst kind. */
static bool command_lba(const ap_awd_geometry_t geometry,
                        const ap_omti_cdb_t *cdb, uint32_t *lba) {
  if ((cdb->control & AP_OMTI_CONTROL_ADDRESS_CONVERSION) != 0u) {
    const uint32_t converted =
        ((uint32_t)cdb->cylinder * AP_OMTI_CONVERSION_HEADS + cdb->head) *
            AP_OMTI_CONVERSION_SECTORS +
        cdb->sector;
    if (converted >= ap_awd_sector_count(geometry)) {
      return false;
    }
    *lba = converted;
    return true;
  }
  return ap_awd_lba(geometry, cdb->cylinder, cdb->head, cdb->sector, lba);
}

/* The address a data command names, and whether the drive has it. */
static bool addressed(ap_omti_t *omti, const ap_omti_cdb_t *cdb,
                      uint32_t *lba) {
  if (omti->selected == NULL) {
    finish(omti, true, SENSE_DRIVE_NOT_READY);
    return false;
  }
  /* §5.2 bit 5, ENABLE SECTOR ADDRESS CONVERSION: "the controller will perform
   * a sector address conversion based on 16 heads per cylinder. The number of
   * sectors per track used in the conversion is based on the SECTORS PER TRACK
   * Jumpers ... This conversion is useful when there is a different number of
   * sectors per track (ESDI) than the DOS is using (17)."
   *
   * So the address in the CDB is in a *host* geometry of sixteen heads and the
   * jumpered sectors per track, and the controller re-expresses it in the
   * drive's own. A linear block is the only thing the two share, so the
   * conversion is: flatten with the conversion geometry, expand with the
   * drive's -- which `ap_awd_lba` already does for the second half.
   *
   * The jumpers are `AP_OMTI_CONVERSION_SECTORS`; see the header for why 18 is
   * this board's, and for the manual's own disagreement about which two jumpers
   * they are. */
  if ((cdb->control & AP_OMTI_CONTROL_ADDRESS_CONVERSION) != 0u) {
    const uint32_t converted =
        ((uint32_t)cdb->cylinder * AP_OMTI_CONVERSION_HEADS + cdb->head) *
            AP_OMTI_CONVERSION_SECTORS +
        cdb->sector;
    if (converted >= ap_awd_sector_count(omti->selected->geometry)) {
      refuse(omti, cdb->cylinder, cdb->head, cdb->sector, 0u,
             SENSE_ILLEGAL_ADDRESS);
      return false;
    }
    *lba = converted;
    return true;
  }
  if (!ap_awd_lba(omti->selected->geometry, cdb->cylinder, cdb->head, cdb->sector,
                  lba)) {
    refuse(omti, cdb->cylinder, cdb->head, cdb->sector, 0u,
           SENSE_ILLEGAL_ADDRESS);
    return false;
  }
  /* Appendix A `19 Bad Track Encountered`: "the specified track has previously
   * been formatted with the BAD TRACK FLAG set in the ID field. It is not
   * possible to access data on this track and the command will be terminated."
   * `07 FORMAT BAD TRACK` sets that flag, and until the sidecar existed there
   * was nowhere to set it -- so `07` was identical to `06` and this refusal
   * could never happen. Both halves are real now.
   *
   * And `1C` for an alternate reached directly: "a direct access to an
   * alternate track was attempted ... the controller was unable to read the
   * alternate track data specifying the destination cylinder." */
  {
    const uint8_t flags = ap_awd_flags(omti->selected, *lba);
    if ((flags & AP_AWD_FLAG_BAD_TRACK) != 0u) {
      finish(omti, true, SENSE_BAD_TRACK);
      return false;
    }
    if ((flags & AP_AWD_FLAG_IS_ALTERNATE) != 0u) {
      finish(omti, true, SENSE_ALTERNATE_DIRECT);
      return false;
    }
  }
  return true;
}

/* Record a sector taken off the surface, whichever command took it.
 *
 * **Every** read path calls this, and until it did only `08 READ`'s did -- so a
 * report that said "1265 sectors read" was counting one command out of four,
 * and the sectors that filled a directory through `1E READ DATA TO BUFFER` were
 * absent from a list that claimed to be what the run had read. A command that
 * fetches the *wrong* sector completes normally and sets no sense bytes, so
 * this list is the only thing in the report that can show it; a list with a
 * whole command missing from it is worse than none, because it reads as
 * evidence of absence. */
static void note_read(ap_omti_t *omti, uint32_t lba) {
  const unsigned kept =
      sizeof omti->recent_reads / sizeof omti->recent_reads[0];
  omti->recent_reads[omti->recent_read_count % kept] = lba;
  omti->recent_read_count++;
}

/* Load the next sector of a read, or end the command when there are none. */
static void feed(ap_omti_t *omti) {
  if (omti->blocks_left == 0u) {
    finish(omti, false, 0u);
    return;
  }
  if (!ap_awd_read(omti->selected, omti->next_lba, omti->buffer)) {
    uint16_t c = 0;
    uint8_t h = 0;
    uint8_t sec = 0;
    chs_of(omti->selected->geometry, omti->next_lba, &c, &h, &sec);
    refuse(omti, c, h, sec, omti->next_lba, SENSE_VOLUME_OVERFLOW);
    return;
  }
  note_read(omti, omti->next_lba);
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
/* Every command that puts something on the surface asks this first.
 *
 * The check is on the *drive*, before any address arithmetic, because a
 * write-protected drive refuses a perfectly good address and the two answers
 * must not be confused -- which is precisely what happened when they were. */
static bool writable(ap_omti_t *omti) {
  if (omti->selected == NULL) {
    finish(omti, true, SENSE_DRIVE_NOT_READY);
    return false;
  }
  if (!omti->selected->writable) {
    finish(omti, true, SENSE_WRITE_PROTECTED);
    return false;
  }
  return true;
}

static unsigned block_count(const ap_omti_cdb_t *cdb) {
  return cdb->block_count == 0u ? 256u : cdb->block_count;
}

/* Enter the data phase with `length` bytes already staged in the buffer, in the
 * direction `to_host` names.
 *
 * §4.3 for both halves of the status: `C/D` clear because these are data bytes,
 * `I/O` set only when they travel to the host, `REQ` because the phase is a
 * handshake, and `DREQ` only when the MASK register put the transfer in DMA
 * mode -- "If the DMA ENABLE bit in the MASK byte has been previously set, data
 * will be transferred in DMA mode ... it will set the DREQ bit".
 *
 * `blocks_left` is zeroed, which is the flag the two byte-transfer paths read:
 * a transfer with no blocks behind it ends when its length is exhausted, and
 * one with blocks continues into the next sector. */
static void transfer(ap_omti_t *omti, unsigned length, bool to_host) {
  omti->buffer_index = 0u;
  omti->transfer_length = length;
  omti->blocks_left = 0u;
  omti->phase = to_host ? AP_OMTI_PHASE_DATA_IN : AP_OMTI_PHASE_DATA_OUT;
  omti->status = (uint8_t)((omti->status & ~(AP_OMTI_ST_CD | AP_OMTI_ST_IO)) |
                           AP_OMTI_ST_REQ);
  if (to_host) {
    omti->status |= AP_OMTI_ST_IO;
  }
  if ((omti->mask & AP_OMTI_MASK_DMA_ENABLE) != 0u) {
    omti->status |= AP_OMTI_ST_DREQ;
  }
}

/* §5.4.4's data pattern, and the whole of what a format does that an `.awd`
 * image can represent.
 *
 * "If B bit 6 of the Control Byte is set to 0, all data fields are written with
 * the pattern `6Ch`. If bit 6 of the Control Byte is set to 1, all data fields
 * are written with the pattern contained in the controller data buffer." The
 * decode packs byte 5's bits 7, 6 and 5 into `control`, so `B` is bit 1 of it.
 *
 * ## What the sidecar now carries, and what it does not
 *
 * A format also writes the **ID field** of every sector -- the track skewing and
 * interleave of bytes 4, and the bad-track and alternate-track flags §5.4.7 and
 * §5.4.16 set. The flags are **now written**, into the `.awdmeta` sidecar
 * (`docs/references/AWD_META.md`), so `07 FORMAT BAD TRACK` differs from `06`
 * and a later access to that track is refused with Appendix A's `19`. Without a
 * sidecar attached nothing is recorded and the surface is defect-free, which is
 * what a bare `.awd` is.
 *
 * The cost is named rather than hidden: a driver that formats a bad track and
 * then expects sense `19 Bad Track Encountered` on the next access to it will
 * see a successful read. Closing it needs an image format carrying ID fields --
 * not this one, and nothing that boots this machine writes bad tracks. Skew and
 * interleave are accepted and ignored for the same reason, and cost nothing at
 * all: they are a placement of sectors around a rotating surface, and this model
 * has no rotation to place them on. */
static bool format_track(ap_omti_t *omti, uint16_t cylinder, uint8_t head,
                         uint8_t control, uint8_t flags) {
  uint8_t sector[AP_AWD_SECTOR_BYTES];
  if ((control & AP_OMTI_CONTROL_FORMAT_BUFFER) != 0u) {
    memcpy(sector, omti->buffer, sizeof sector);
  } else {
    memset(sector, 0x6Cu, sizeof sector);
  }
  for (uint16_t s = 0; s < omti->selected->geometry.sectors; s++) {
    uint32_t at = 0;
    if (!ap_awd_lba(omti->selected->geometry, cylinder, head, (uint8_t)s, &at) ||
        !ap_awd_write(omti->selected, at, sector)) {
      return false;
    }
    /* The ID field, which the image now has somewhere to keep. A format
     * *clears* the flags of every sector it writes -- that is what makes `06`
     * usable to un-mark a track `07` marked -- and `07` sets the bad-track bit
     * afterwards. A sidecar that is absent or does not cover this sector
     * silently records nothing, which is the same surface `ap_awd_flags`
     * describes when it returns zero. */
    (void)ap_awd_set_flags(omti->selected, at, flags);
  }
  return true;
}

static void execute(ap_omti_t *omti) {
  ap_omti_cdb_t cdb;
  ap_omti_cdb_decode(omti->command, &cdb);
  omti->last_command = cdb.command;
  omti->command_count++;
  /* §5.1.1, byte 1: "Bit 5 identifies the Logical Unit Number (LUN)." The field
   * was decoded and asserted by a test from the day it was written, and never
   * read here -- so every command was served by whichever drive happened to be
   * attached. Measured against the oracle on a Domain/OS boot: `00 TEST DRIVE
   * READY` for LUN 1 *succeeded*, the firmware printed `DRIVE 1 PASSED.` where
   * a real controller prints `(NOT FOUND)`, and 271 later reads addressed to a
   * drive that is not fitted were answered out of drive 0's image. */
  omti->command_lun = cdb.lun;
  omti->selected = cdb.lun == 0u ? omti->drive : NULL;
  {
    /* Recorded before anything is checked, so a refused command is in the log
     * too: "the command that was rejected" is as much of the sequence as the
     * ones that ran. The address is the CDB's own, which for the buffer
     * commands is meaningless and is recorded as absent rather than as zero. */
    const unsigned kept =
        sizeof omti->recent_commands / sizeof omti->recent_commands[0];
    const unsigned at = omti->recent_command_count % kept;
    uint32_t where = UINT32_MAX;
    if (omti->selected != NULL &&
        cdb.command != AP_OMTI_CMD_READ_SECTOR_BUFFER &&
        cdb.command != AP_OMTI_CMD_WRITE_SECTOR_BUFFER) {
      uint32_t at_lba = 0;
      if (command_lba(omti->selected->geometry, &cdb, &at_lba)) {
        where = at_lba;
      }
    }
    omti->recent_commands[at].command = cdb.command;
    omti->recent_commands[at].blocks = (uint16_t)block_count(&cdb);
    omti->recent_commands[at].lba = where;
    omti->recent_commands[at].drained = 0u;
    omti->recent_command_count++;
  }
  /* Any command at all ends whatever long write was outstanding. A WRITE LONG
   * the host abandoned mid-phase must not place its half-filled buffer when
   * some later data-out phase happens to complete. `WRITE LONG` sets it again
   * below, after its address has been checked. */
  omti->long_write_blocks = 0u;
  omti->assigning_alternate = false;

  if (!ap_omti_cdb_accepted_by_esdi(cdb.command)) {
    /* Including `0C INITIALIZE DRIVE CHARACTERISTICS`, which is ST506-only and
     * which this controller must refuse rather than quietly accept. Appendix
     * A's `20` word for word: "the controller decoded a command code that it
     * does not support". */
    finish(omti, true, SENSE_INVALID_COMMAND);
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
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->next_lba = lba;
    omti->blocks_left = block_count(&cdb);
    omti->buffer_index = 0u;
    omti->phase = AP_OMTI_PHASE_DATA_OUT;
    /* The data state asks for its first word like every other, and this is the
     * one command that entered it by hand instead of through `transfer` -- so
     * it inherited neither of the two rules that path already had.
     *
     * **`REQ` was never asserted at all.** §4.3: "When the controller requires a
     * word to be transferred, it will set the REQ bit in the STATUS byte", and a
     * driver doing programmed I/O writes nothing until it sees that bit. This
     * left the controller in `DATA OUT` at `C8` -- selected and busy, asking for
     * nothing -- which is exactly the byte `WIN_$SPIN_DOWN` polled 3,301 times
     * before returning `00080003`, disk controller time-out.
     *
     * **And `DREQ` was unconditional**, which is the defect the read path had
     * and had fixed: §4.2 gates it on the MASK register's DMA enable, so a
     * controller in programmed I/O was asking the 8237 for cycles nobody had
     * arranged. Two lines below, `REQUEST SENSE` names that same fix. */
    omti->status = (uint8_t)((omti->status & ~(AP_OMTI_ST_CD | AP_OMTI_ST_IO)) |
                             AP_OMTI_ST_REQ);
    if ((omti->mask & AP_OMTI_MASK_DMA_ENABLE) != 0u) {
      omti->status |= AP_OMTI_ST_DREQ;
    }
    return;

  case AP_OMTI_CMD_REQUEST_SENSE:
    /* The four bytes the *previous* command left, which is the whole point of
     * the command: a driver reads it after a failure to learn what failed. */
    memcpy(omti->buffer, omti->sense, sizeof omti->sense);
    /* Requested, and travelling to the host. `DREQ` only in DMA mode -- this
     * asserted it unconditionally, the same defect the read path had. */
    transfer(omti, (unsigned)sizeof omti->sense, true);
    return;

  case AP_OMTI_CMD_TEST_DRIVE_READY:
    finish(omti, omti->selected == NULL, SENSE_DRIVE_NOT_READY);
    return;

  case AP_OMTI_CMD_READ_CONFIGURATION: {
    /* §5.4.29, ten bytes describing the drive. ESDI only, which
     * `ap_omti_cdb_accepted_by_esdi` has already checked. */
    if (omti->selected == NULL) {
      finish(omti, true, SENSE_DRIVE_NOT_READY);
      return;
    }
    const ap_awd_geometry_t g = omti->selected->geometry;
    /* **One less than the count**, as the manual marks them. A model returning
     * the counts describes a drive one cylinder, one head and one sector larger
     * than it has. */
    const uint16_t highest_cylinder = (uint16_t)(g.cylinders - 1u);
    memset(omti->buffer, 0, AP_OMTI_CONFIGURATION_BYTES);
    omti->buffer[0] = (uint8_t)(highest_cylinder >> 8);
    omti->buffer[1] = (uint8_t)highest_cylinder;
    omti->buffer[2] = (uint8_t)(g.heads - 1u);
    omti->buffer[3] = (uint8_t)(g.sectors - 1u);
    /* The **drive configuration word**, bytes 4 and 5, which the manual names
     * and does not define for this drive. The resolution order ran out at the
     * document and the oracle answers: `omti8621.cpp`'s `set_configuration_data`
     * writes `02 44` for every drive it configures.
     *
     * That same function corroborates bytes 0-3 independently -- it computes
     * `(cylinders - 1) >> 8`, `(cylinders - 1) & 0xff`, `heads - 1` and
     * `sectors - 1`, which is the "(-1)" reading taken from the page image
     * before this was looked at. Two sources, one arrived at from the table and
     * one from a running model, agreeing field by field.
     *
     * Bytes 6-9 -- the inter-sector gaps and the PLO sync fields -- are zero
     * there too, so this core's zeros are the oracle's answer rather than an
     * omission. */
    omti->buffer[4] = 0x02u;
    omti->buffer[5] = 0x44u;
    /* Bytes 6-9 stay zero, as above. */
    transfer(omti, AP_OMTI_CONFIGURATION_BYTES, true);
    return;
  }

  case AP_OMTI_CMD_READ_VERIFY:
    /* §5.1.2 gives it zero data bytes: it reads and checks without transferring
     * anything, so what it reports is whether the sectors are *there*. The
     * address is checked and every block is read, because a verify that did not
     * read would answer for a disk it never touched. */
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    for (unsigned i = 0; i < (cdb.block_count == 0u ? 256u : cdb.block_count); i++) {
      note_read(omti, lba + i);
      if (!ap_awd_read(omti->selected, lba + i, omti->buffer)) {
        /* The command's own address was accepted above, so a block that is not
         * there is the end of the volume arriving mid-verify -- `23`, not `21`.
         * And the address is reported, which it was not: this arm ended the
         * command with a bare sense byte and left bytes 1-3 zero with §5.4.3's
         * validity bit clear, telling a driver the controller did not know
         * where the verify stopped. It does know. */
        uint16_t c = 0;
        uint8_t h = 0;
        uint8_t sec = 0;
        chs_of(omti->selected->geometry, lba + i, &c, &h, &sec);
        refuse(omti, c, h, sec, lba + i, SENSE_VOLUME_OVERFLOW);
        return;
      }
    }
    finish(omti, false, 0u);
    return;

  case AP_OMTI_CMD_READ_ID: {
    /* §5.4.24: the ID field of the addressed sector, four bytes. It is the
     * address written back in the format the *disk* carries, which is why a
     * driver uses it to find out where a head actually is. */
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->buffer[0] = (uint8_t)((cdb.cylinder >> 8) & 0x07u);
    omti->buffer[1] = (uint8_t)(cdb.cylinder & 0xFFu);
    /* Flags clear: a raw sector image has no bad tracks and no alternates. */
    omti->buffer[2] = (uint8_t)(cdb.head & 0x0Fu);
    omti->buffer[3] = cdb.sector;
    transfer(omti, AP_OMTI_READ_ID_BYTES, true);
    return;
  }

  case AP_OMTI_CMD_RAM_DIAGNOSTICS:
  case AP_OMTI_CMD_CONTROLLER_DIAGNOSTIC:
    /* §5.4.23 "performs a pattern test on the internal controller buffer" and
     * §5.4.26 a ROM checksum, RAM test and Z8 self test. Both are tests of the
     * *controller*, and this one has no fault to report -- a model that failed
     * them would be claiming a defect it does not have. Neither touches a
     * drive, so neither needs one. */
    finish(omti, false, 0u);
    return;

  case AP_OMTI_CMD_DRIVE_DIAGNOSTIC:
    /* §5.4.25: "recalibrate, sequentially seek to every track and read sector
     * 0". It needs a drive, and it reports what reading sector 0 of every track
     * would -- which for a whole image is success and for a short one is not. */
    if (omti->selected == NULL) {
      finish(omti, true, SENSE_DRIVE_NOT_READY);
      return;
    }
    for (uint16_t c = 0; c < omti->selected->geometry.cylinders; c++) {
      uint32_t at = 0;
      if (!ap_awd_lba(omti->selected->geometry, c, 0u, 1u, &at)) {
        finish(omti, true, SENSE_ILLEGAL_ADDRESS);
        return;
      }
      note_read(omti, at);
      if (!ap_awd_read(omti->selected, at, omti->buffer)) {
        finish(omti, true, SENSE_ILLEGAL_ADDRESS);
        return;
      }
    }
    finish(omti, false, 0u);
    return;

  case AP_OMTI_CMD_READ_SECTOR_BUFFER: {
    /* §5.4.13. "The controller does not access the disk drive during the
     * execution of this command" -- so no `addressed` check and no drive is
     * needed, which is what lets the boot PROM run it before it knows whether a
     * disk is fitted at all. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      /* Past the manual's own cap for this sector size. Refused rather than
       * truncated: a host told a transfer succeeded when it was cut short would
       * read the tail of the previous command's buffer as data. */
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    transfer(omti, blocks * AP_AWD_SECTOR_BYTES, true);
    return;
  }

  case AP_OMTI_CMD_WRITE_SECTOR_BUFFER: {
    /* §5.4.14 WRITE DATA TO SECTOR BUFFER, and `0E` read backwards: "data to be
     * written from the host to the controllers buffer", with the same
     * sector-size table capping it at seven blocks of 1056 bytes, and the same
     * sentence -- "the controller does not access the disk drive during the
     * execution of this command". So no address is checked and no drive is
     * needed, and the data phase runs from the host inwards.
     *
     * The manual numbers §5.4.15 CHECK TRACK FORMAT `0Fh` as well, which cannot
     * be: its own byte-0 row reads `0 0 0 1 0 0 0 0`. The bit pattern is the
     * command, the heading is the typo, and this arm takes the opcode the table
     * gives it. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    /* Data, travelling *to* the controller, and with **no blocks** behind it,
     * which is how the data phase knows this fills the buffer rather than the
     * disk. `0E` distinguishes the two with the same field. */
    transfer(omti, blocks * AP_AWD_SECTOR_BYTES, false);
    return;
  }

  case AP_OMTI_CMD_READ_TO_BUFFER: {
    /* §5.4.19 READ DATA TO BUFFER: "This command reads data from the disk to
     * the controller's buffer. **It does not transfer the data to the host.**"
     * The host collects it afterwards with `0E READ DATA FROM SECTOR BUFFER`,
     * and §5.4.13 names the pairing from the other end -- `0E` "is normally
     * used immediately after a Read Data to Sector Buffer (1Eh) command has
     * been issued to enhance performance when data transfers are done using
     * programmed I/O".
     *
     * So this fills the buffer and ends in the status phase with no data phase
     * at all. Getting that wrong in the other direction -- offering the data to
     * the host -- would leave a driver reading bytes it never asked for and the
     * controller waiting for a handshake that never comes.
     *
     * Domain/OS issues exactly this pair, once each, and this command being
     * unimplemented is what crashed the machine: it fell to the default arm and
     * reported `SENSE_ILLEGAL_ADDRESS`, which the operating system's jump table
     * turns into a fatal status. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      /* The same cap as `0E`, and §5.4.19 prints the same table for it: seven
       * blocks at 1056 bytes. */
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    for (unsigned block = 0; block < blocks; block++) {
      if (!ap_awd_read(omti->selected, lba + block,
                       &omti->buffer[block * AP_AWD_SECTOR_BYTES])) {
        refuse(omti, cdb.cylinder, cdb.head, cdb.sector, lba + block,
               SENSE_VOLUME_OVERFLOW);
        return;
      }
      note_read(omti, lba + block);
    }
    finish(omti, false, 0u);
    return;
  }

  case AP_OMTI_CMD_FORMAT_DRIVE: {
    /* §5.4.4: "causes the specified Logical Unit Number (LUN) to be formatted
     * ... Formatting starts at the specified track and proceeds until the last
     * track of the unit is formatted." Its descriptor addresses a *track*:
     * byte 2 carries only C09/C08 and no sector number, and byte 4 is the track
     * skewing and interleave factor rather than a block count.
     *
     * It really does write the whole drive from that track on. That is the
     * command, and a model that quietly declined would be the more dangerous
     * one -- a driver told a format succeeded goes on to trust the surface. */
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    for (uint16_t c = cdb.cylinder; c < omti->selected->geometry.cylinders; c++) {
      /* The first cylinder starts at the addressed head; every later one starts
       * at head 0, because the format is a sweep of the unit and not of one
       * cylinder's tail repeated. */
      const uint8_t first = (c == cdb.cylinder) ? cdb.head : 0u;
      for (uint8_t h = first; h < omti->selected->geometry.heads; h++) {
        if (!format_track(omti, c, h, cdb.control, 0u)) {
          refuse(omti, c, h, 0u, 0u, SENSE_ILLEGAL_ADDRESS);
          return;
        }
      }
    }
    finish(omti, false, 0u);
    return;
  }

  case AP_OMTI_CMD_FORMAT_TRACK:
  case AP_OMTI_CMD_FORMAT_BAD_TRACK:
    /* §5.4.6, "causes the track specified to be formatted", and §5.4.7, "this
     * command is identical to the FORMAT TRACK command except that the
     * defective track flag is set in the ID field". The one difference between
     * them is the flag, and the flag is the part an `.awd` image cannot hold --
     * see `format_track` above, where the omission is set out in full. They are
     * one arm here because in this model they genuinely do the same thing, and
     * splitting them would suggest a distinction that is not being made. */
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    if (!format_track(omti, cdb.cylinder, cdb.head, cdb.control,
                      cdb.command == AP_OMTI_CMD_FORMAT_BAD_TRACK
                          ? AP_AWD_FLAG_BAD_TRACK
                          : 0u)) {
      refuse(omti, cdb.cylinder, cdb.head, 0u, 0u, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    finish(omti, false, 0u);
    return;

  case AP_OMTI_CMD_ASSIGN_ALTERNATE:
    /* §5.4.16: the alternate track's address arrives in a **data-out** phase of
     * "4 bytes (2 words)" after the command, and the controller then "sets
     * flags in the ID field and writes the alternate track address in all
     * blocks on the specified track. The alternate track is then formatted".
     *
     * The address of the track being replaced is checked now; the alternate's
     * own address arrives with the data, and the format happens when it does. */
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->assigning_alternate = true;
    transfer(omti, AP_OMTI_ALTERNATE_ADDRESS_BYTES, false);
    return;

  case AP_OMTI_CMD_COPY: {
    /* §5.4.21: "copies a specified number of blocks (byte 4) from a Source LUN
     * to a Destination LUN ... **No data is transferred to the host.**" Ten
     * bytes rather than six, which `ap_omti_cdb_length` already knew, and the
     * destination address occupies bytes 5-7 in the same three-byte layout the
     * source uses in bytes 1-3 -- so it is decoded by handing those bytes to
     * the same decoder rather than by unpacking eleven bits of cylinder a
     * second time by hand.
     *
     * "Source and Destination LUN's may be the same", and this controller has
     * one drive, so they always are here. Copying through the staging buffer a
     * block at a time is what makes an overlapping copy behave: each block is
     * read before the block it overwrites is needed. */
    ap_omti_cdb_t destination;
    uint8_t bytes[6] = {0};
    memcpy(&bytes[1], &omti->command[5], 3u);
    ap_omti_cdb_decode(bytes, &destination);
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    uint32_t to = 0;
    if (!ap_awd_lba(omti->selected->geometry, destination.cylinder,
                    destination.head, destination.sector, &to)) {
      refuse(omti, destination.cylinder, destination.head, destination.sector,
             0u, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    const unsigned blocks = block_count(&cdb);
    for (unsigned block = 0; block < blocks; block++) {
      note_read(omti, lba + block);
      if (!ap_awd_read(omti->selected, lba + block, omti->buffer) ||
          !ap_awd_write(omti->selected, to + block, omti->buffer)) {
        refuse(omti, cdb.cylinder, cdb.head, cdb.sector, lba + block,
               SENSE_VOLUME_OVERFLOW);
        return;
      }
    }
    finish(omti, false, 0u);
    return;
  }

  case AP_OMTI_CMD_WRITE_FROM_BUFFER: {
    /* §5.4.20, and `1E` in the other direction: "writes data from the
     * controller's buffer to the disk. The number of sectors written is
     * specified by the block count parameter but is limited by the controller's
     * buffer size ... An error will be returned if the block count exceeds the
     * above limits." No host data phase -- the bytes are already staged, by an
     * earlier `0F`. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    for (unsigned block = 0; block < blocks; block++) {
      if (!ap_awd_write(omti->selected, lba + block,
                        &omti->buffer[block * AP_AWD_SECTOR_BYTES])) {
        refuse(omti, cdb.cylinder, cdb.head, cdb.sector, lba + block,
               SENSE_VOLUME_OVERFLOW);
        return;
      }
    }
    finish(omti, false, 0u);
    return;
  }

  case AP_OMTI_CMD_READ_ECC_LENGTH:
    /* §5.4.12 READ ECC BURST ERROR LENGTH: "returns one word of data ... This
     * word contains the length of the ECC error detected during the most recent
     * correctable data field error", and is "used in conjunction with the
     * Disable ECC bit set on a READ command".
     *
     * A word in the prose and a single byte in the table below it -- "ECC ERROR
     * LENGTH BYTE FORMAT", byte 0, ECC error length. Both are satisfied by a
     * two-byte transfer whose first byte is the length, which is what a 16-bit
     * data port moves in one cycle, so this is a reading of the two rather than
     * a choice between them.
     *
     * The length is **zero**, and that is a fact about this model rather than a
     * placeholder: an `.awd` image is sector data with no ECC field, no read of
     * it can produce a correctable error, so there has never been a most-recent
     * one to report. A non-zero answer would be inventing a media defect. */
    omti->buffer[0] = 0u;
    omti->buffer[1] = 0u;
    transfer(omti, 2u, true);
    return;

  case AP_OMTI_CMD_READ_ESDI_DEFECT_LIST: {
    /* §5.4.22: "return 256 bytes of drive manufacturer recorded DEFECT LIST
     * during the Data In phase ... Only the list for the specified HEAD will be
     * returned." Bytes 0-2 are the date the list was recorded, byte 3 the head,
     * bytes 4-5 zero; then five-byte descriptors; then "Five FFh bytes indicate
     * the end of the DEFECT LIST".
     *
     * An `.awd` image is a defect-free surface -- it is sector data, with no
     * medium underneath it to have defects -- so the list is empty and the
     * terminator follows the header directly. The date is zero because nothing
     * recorded one; inventing today's would also make the reply depend on the
     * wall clock, which nothing in this core is allowed to do. */
    if (omti->selected == NULL) {
      finish(omti, true, SENSE_DRIVE_NOT_READY);
      return;
    }
    memset(omti->buffer, 0, AP_OMTI_DEFECT_LIST_BYTES);
    omti->buffer[3] = cdb.head;
    for (unsigned i = 0; i < 5u; i++) {
      omti->buffer[6u + i] = 0xFFu;
    }
    transfer(omti, AP_OMTI_DEFECT_LIST_BYTES, true);
    return;
  }

  case AP_OMTI_CMD_READ_LONG: {
    /* §5.4.27: "returns the Block size equal to the jumper selected sector size
     * (512, 1024 or 1056) of data plus 4 bytes (for ST506/412 drives) or 6
     * bytes (for ESDI drives) of ECC data." This machine's drives are ESDI, so
     * a long block is 1062 bytes.
     *
     * **Deliberate approximation**: the six ECC bytes are zero. An `.awd` image
     * stores sector data and nothing else, so there is no recorded ECC to
     * return and the polynomial is not published in this manual -- any value
     * here would be invented. Zero is the one that says "none recorded". The
     * cost to close is an image format that carries ECC, which nothing this
     * machine runs would read; the risk is a diagnostic that checks the ECC of
     * a sector it has just written with WRITE LONG, which would see zeros. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    memset(omti->buffer, 0, blocks * AP_OMTI_LONG_BLOCK_BYTES);
    for (unsigned block = 0; block < blocks; block++) {
      /* The six ECC bytes as *recorded*, now that the sidecar can hold them.
       * A sector never written by WRITE LONG reads zeros, which is "none
       * recorded" -- `[OMTI]` publishes no polynomial, so a value computed here
       * would be indistinguishable from a real one. */
      ap_awd_ecc(omti->selected, lba + block,
                 &omti->buffer[block * AP_OMTI_LONG_BLOCK_BYTES +
                               AP_AWD_SECTOR_BYTES]);
      note_read(omti, lba + block);
      if (!ap_awd_read(omti->selected, lba + block,
                       &omti->buffer[block * AP_OMTI_LONG_BLOCK_BYTES])) {
        refuse(omti, cdb.cylinder, cdb.head, cdb.sector, lba + block,
               SENSE_VOLUME_OVERFLOW);
        return;
      }
    }
    transfer(omti, blocks * AP_OMTI_LONG_BLOCK_BYTES, true);
    return;
  }

  case AP_OMTI_CMD_WRITE_LONG: {
    /* §5.4.28, the same block width inbound: sector plus six ECC bytes. The
     * ECC is accepted and dropped, for the reason `READ LONG` returns zeros --
     * the image has nowhere to keep it. The address is checked *before* the
     * data phase so a host is not asked for 1062 bytes the controller has
     * already decided it cannot place. */
    const unsigned blocks = block_count(&cdb);
    if (blocks > AP_OMTI_MAX_BUFFER_BLOCKS) {
      finish(omti, true, SENSE_ILLEGAL_ADDRESS);
      return;
    }
    if (!writable(omti) || !addressed(omti, &cdb, &lba)) {
      return;
    }
    omti->long_write_lba = lba;
    omti->long_write_blocks = blocks;
    transfer(omti, blocks * AP_OMTI_LONG_BLOCK_BYTES, false);
    return;
  }

  case AP_OMTI_CMD_CHECK_TRACK_FORMAT: {
    /* §5.4.15, "Valid for ESDI drives only": "checks the integrity of the track
     * specified against CRC, ECC value". Its descriptor addresses a *track* --
     * byte 2's sector field is a zero value, unlike every other addressed
     * command -- so the check runs over the whole track, and reading every
     * sector of it is the strongest statement this model can make about that
     * track's integrity. §5.4.4 names it as one of the two ways to verify a
     * format, which is what a caller is asking. */
    if (!addressed(omti, &cdb, &lba)) {
      return;
    }
    for (uint16_t s = 0; s < omti->selected->geometry.sectors; s++) {
      uint32_t at = 0;
      if (!ap_awd_lba(omti->selected->geometry, cdb.cylinder, cdb.head,
                      (uint8_t)s, &at)) {
        refuse(omti, cdb.cylinder, cdb.head, (uint8_t)s, at,
               SENSE_ILLEGAL_ADDRESS);
        return;
      }
      note_read(omti, at);
      if (!ap_awd_read(omti->selected, at, omti->buffer)) {
        refuse(omti, cdb.cylinder, cdb.head, (uint8_t)s, at,
               SENSE_ILLEGAL_ADDRESS);
        return;
      }
    }
    finish(omti, false, 0u);
    return;
  }

  case AP_OMTI_CMD_START_STOP:
    /* §5.4.17, "Valid for ESDI drives only", and an ESDI command this core did
     * not accept at all until §5.4 was read end to end rather than one command
     * at a time. "To start the unit, the Start bit shall be set to one. To stop
     * the unit, the Start bit shall be set to zero. This command returns status
     * immediately after receiving the command bytes, then does not wait for the
     * start or stop spindle operation to complete."
     *
     * Returning immediately is the whole of the behaviour, and it is the part
     * a model can get *wrong* by being helpful: a controller that waited would
     * hold the bus for a spindle spin-up the host was told not to wait for.
     * There is no spindle here to start, and none of §5's other commands
     * consults one, so nothing is recorded -- a stopped-drive state this model
     * could never leave would refuse reads the hardware would serve. */
    if (omti->selected == NULL) {
      finish(omti, true, SENSE_DRIVE_NOT_READY);
      return;
    }
    finish(omti, false, 0u);
    return;

  case AP_OMTI_CMD_CHANGE_CARTRIDGE:
    /* §5.4.18: "valid only for Removable disk drives". Appendix A supplies the
     * answer for every other kind in `22`'s own description -- "a Change
     * Cartridge command (HEX 1B) was issued to a LUN assigned as a Fixed drive
     * type". The DN3500's Winchester is fixed, so this command always fails
     * here, and it fails for a documented reason rather than a default. */
    finish(omti, true, SENSE_ILLEGAL_FUNCTION);
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
     * when nothing was written would go on to trust the disk.
     *
     * `20` rather than `21`, which is the honest one of the two: this
     * controller does not support the code. Told `21` instead, Domain/OS was
     * being informed its geometry was wrong -- and it believed it, took a
     * recovery path built for that, and died several layers away from the
     * command that failed. Two commands, `1E` and `0F`, had to be excavated
     * from that distance one at a time. A model that reports why it failed in
     * the vocabulary the host already understands is the difference between a
     * census and an excavation. */
    finish(omti, true, SENSE_INVALID_COMMAND);
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
    /* No blocks to place means the buffer itself is the destination -- `0F`,
     * whose §5.4.14 does not access the drive. The mirror of the test the
     * `DATA_IN` path makes on the same field, and the reason it is a *test*
     * rather than a flag: a phase that ends by writing to a disk and one that
     * ends by not writing to a disk differ in exactly this. */
    if (omti->blocks_left == 0u) {
      if (omti->buffer_index < omti->transfer_length) {
        /* §4.3's data state, and the sentence this phase was missing: "When the
         * controller requires a word to be transferred, it will set the REQ bit
         * in the STATUS byte ... Either action will cause REQ to be cleared.
         * These steps will be repeated until all the data required by the
         * controller has been transferred", and it is "handshaking in the same
         * fashion as the command transfer".
         *
         * `take_byte` clears `REQ` for every write, so a path that wants another
         * byte has to ask for it again. The command phase does, and so does the
         * `DATA IN` side in `give_byte` -- **this one did not**, so the
         * controller took one byte and then sat with `REQ` down, which is the
         * status byte `C8` that `WIN_$SPIN_DOWN` polled 3,301 times before
         * giving up with `00080003`, disk controller time-out. Nothing caught it
         * because every test here writes its bytes without consulting `REQ`. */
        omti->status |= AP_OMTI_ST_REQ;
        return;
      }
      if (omti->assigning_alternate) {
        /* §5.4.16's descriptor has arrived. Its head and cylinder occupy the
         * same field positions a descriptor block's bytes 1-3 do, so the same
         * decoder reads it, and the alternate track "is then formatted".
         *
         * The flags this command sets in the ID field are the part an `.awd`
         * image cannot carry -- `format_track` sets that out. What it can carry
         * is the format itself, and that is what happens here. */
        uint8_t bytes[6] = {0};
        memcpy(&bytes[1], omti->buffer, 3u);
        ap_omti_cdb_t alternate;
        ap_omti_cdb_decode(bytes, &alternate);
        omti->assigning_alternate = false;
        if (!format_track(omti, alternate.cylinder, alternate.head, 0u,
                          AP_AWD_FLAG_IS_ALTERNATE)) {
          refuse(omti, alternate.cylinder, alternate.head, 0u, 0u,
                 SENSE_ILLEGAL_ADDRESS);
          return;
        }
        finish(omti, false, 0u);
        return;
      }
      /* Staged, and now placed if a WRITE LONG asked for it. `0F` leaves
       * `long_write_blocks` at zero and ends here having written nothing, which
       * is §5.4.14's "does not access the disk drive"; §5.4.28 does access it,
       * one 1062-byte block at a time of which only the leading sector is kept.
       *
       * The six ECC bytes are dropped. They have nowhere to go -- an `.awd`
       * image is sector data with no ECC field -- and dropping them is the same
       * deliberate approximation READ LONG's zeros are, named here so the two
       * halves of it are not documented in only one place. */
      for (unsigned block = 0; block < omti->long_write_blocks; block++) {
        /* And the ECC is **kept**, not dropped: §5.4.28 hands the controller
         * six bytes and the sidecar is where they live. This used to discard
         * them for want of anywhere to put them. */
        (void)ap_awd_set_ecc(omti->selected, omti->long_write_lba + block,
                             &omti->buffer[block * AP_OMTI_LONG_BLOCK_BYTES +
                                           AP_AWD_SECTOR_BYTES]);
        if (!ap_awd_write(omti->selected, omti->long_write_lba + block,
                          &omti->buffer[block * AP_OMTI_LONG_BLOCK_BYTES])) {
          omti->long_write_blocks = 0u;
          /* Past the end of the volume, and the host has already handed over
           * every byte -- as commenced as a command gets. */
          uint16_t c = 0;
          uint8_t h = 0;
          uint8_t sec = 0;
          chs_of(omti->selected->geometry, omti->long_write_lba + block, &c, &h,
                 &sec);
          refuse(omti, c, h, sec, omti->long_write_lba + block,
                 SENSE_VOLUME_OVERFLOW);
          return;
        }
      }
      omti->long_write_blocks = 0u;
      finish(omti, false, 0u);
      return;
    }
    /* A *sector*, which is what the write path is waiting for -- not the whole
     * buffer, which now holds several. The two were the same number until
     * `0E` needed room for seven, and the constant used here was the wrong one
     * of the two all along. */
    if (omti->buffer_index < AP_AWD_SECTOR_BYTES) {
      /* The same handshake, mid-sector. */
      omti->status |= AP_OMTI_ST_REQ;
      return;
    }
    if (!ap_awd_write(omti->selected, omti->next_lba, omti->buffer)) {
      /* The streaming WRITE's counterpart to `feed`'s refusal on the read side,
       * and the same code for the same reason: the descriptor block's address
       * was accepted, the host has been handing over sectors, and this one has
       * run off the end. */
      uint16_t c = 0;
      uint8_t h = 0;
      uint8_t sec = 0;
      chs_of(omti->selected->geometry, omti->next_lba, &c, &h, &sec);
      refuse(omti, c, h, sec, omti->next_lba, SENSE_VOLUME_OVERFLOW);
      return;
    }
    omti->next_lba++;
    omti->blocks_left--;
    omti->buffer_index = 0u;
    if (omti->blocks_left == 0u) {
      finish(omti, false, 0u);
      return;
    }
    /* And across a sector boundary: the next sector's first byte is still data
     * "required by the controller", so it is asked for like every other. */
    omti->status |= AP_OMTI_ST_REQ;
    return;

  case AP_OMTI_PHASE_DATA_IN:
  case AP_OMTI_PHASE_STATUS:
  case AP_OMTI_PHASE_EXECUTING:
    /* A write while the controller is talking, or while the drive is still
     * positioning. Ignored rather than merged into the stream: the bus is the
     * controller's in these phases. */
    return;
  }
}

/* A byte the host read from the data port. */
static void note_drained(ap_omti_t *omti) {
  if (omti->recent_command_count == 0u) {
    return;
  }
  const unsigned kept =
      sizeof omti->recent_commands / sizeof omti->recent_commands[0];
  omti->recent_commands[(omti->recent_command_count - 1u) % kept].drained++;
}

static uint8_t give_byte(ap_omti_t *omti) {
  switch (omti->phase) {
  case AP_OMTI_PHASE_DATA_IN: {
    const uint8_t value = omti->buffer[omti->buffer_index++];
    note_drained(omti);
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
  case AP_OMTI_PHASE_EXECUTING:
    /* Nothing to give: in `EXECUTING` the drive has not reached the sector yet,
     * and `REQ` is down to say so. A driver polling the data register here gets
     * the last value the register held, which is what a bus with no new byte on
     * it presents. */
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

uint16_t ap_omti_disk_read16(ap_omti_t *omti) {
  /* Two bytes out of the same stream a byte read would take, in one cycle, and
   * the **earlier byte in the high half** -- so a buffer moved by word accesses
   * lands in memory in the order it has on the disk. See the header. */
  const uint16_t high = give_byte(omti);
  const uint16_t low = give_byte(omti);
  return (uint16_t)((high << 8) | low);
}

void ap_omti_disk_write16(ap_omti_t *omti, uint16_t value) {
  omti->data = value;
  take_byte(omti, (uint8_t)(value >> 8));
  take_byte(omti, (uint8_t)(value & 0xFFu));
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
    /* Disabling either enable takes its request bit down with it. A driver
     * turning interrupts off and then polling must not find `IREQ` standing
     * from a command that completed while they were on -- it would read as a
     * completion that had already been collected. `omti8621.cpp` does the same
     * on this write, for both bits. */
    if ((value & AP_OMTI_MASK_INTERRUPT_ENABLE) == 0u) {
      omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_IREQ);
    }
    if ((value & AP_OMTI_MASK_DMA_ENABLE) == 0u) {
      omti->status = (uint8_t)(omti->status & ~AP_OMTI_ST_DREQ);
    }
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

bool ap_omti_fdc_irq(const ap_omti_t *omti) {
  /* Gated on the Digital Output Register's interrupt/DMA enable, as the fixed
   * disk's `IREQ` is gated on the MASK register -- Table 4-3 bit 3. A driver
   * that has not enabled it must not see the line, or a polled driver would be
   * interrupted by a controller it never armed. */
  if ((omti->dor & AP_OMTI_DOR_INT_DMA) == 0u) {
    return false;
  }
  /* The result phase is the FDC's completion: its bytes are waiting to be
   * read. Commands with no result phase raise nothing, which is correct --
   * SEEK and RECALIBRATE report through SENSE INTERRUPT STATUS instead. */
  return omti->fdc_phase == AP_OMTI_PHASE_STATUS &&
         omti->fdc_result_index < omti->fdc_result_length;
}

bool ap_omti_fdc_dma_request(const ap_omti_t *omti) {
  if ((omti->dor & AP_OMTI_DOR_INT_DMA) == 0u) {
    return false;
  }
  /* The execution phase, where a byte is ready to move -- a different
   * condition from the fixed disk's `DREQ`, which the board's comment named
   * and this is. */
  return omti->fdc_phase == AP_OMTI_PHASE_DATA_IN ||
         omti->fdc_phase == AP_OMTI_PHASE_DATA_OUT;
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

/* Table 4-3's motor enables. A floppy command reaches the medium only with the
 * selected drive's motor running -- which is what the bits are *for*, and they
 * were stored and never read, so a driver that never spun a motor up got its
 * data anyway.
 *
 * Reported rather than enforced in the data path: this core has no spin-up time
 * and no medium that stops turning, so refusing a command here would model a
 * failure the model cannot otherwise produce. The bit says what the board
 * asked; whether a caller acts on it is the caller's. */
bool ap_omti_fdc_motor_on(const ap_omti_t *omti, unsigned drive) {
  const uint8_t bit = drive == 0u ? AP_OMTI_DOR_DRIVE_A_MOTOR
                                  : AP_OMTI_DOR_DRIVE_B_MOTOR;
  return (omti->dor & bit) != 0u;
}

uint8_t ap_omti_fdc_precompensation(const ap_omti_t *omti) {
  return (uint8_t)(omti->fdc_control & AP_OMTI_FDC_CONTROL_PRECOMP_MASK);
}

bool ap_omti_fdc_control_pin(const ap_omti_t *omti, unsigned pin) {
  /* `002398-04` p. 12-14 names three, at 3, 4 and 5, and calls them by the
   * interface pin each drives -- 2, 4 and 6. Asking by the pin number is how the
   * page reads; anything else is not a pin this register has. */
  switch (pin) {
  case 2u:
    return (omti->fdc_control & AP_OMTI_FDC_CONTROL_PIN2) != 0u;
  case 4u:
    return (omti->fdc_control & AP_OMTI_FDC_CONTROL_PIN4) != 0u;
  case 6u:
    return (omti->fdc_control & AP_OMTI_FDC_CONTROL_PIN6) != 0u;
  default:
    return false;
  }
}

ap_omti_fdc_rate_t ap_omti_fdc_data_rate(const ap_omti_t *omti) {
  return (ap_omti_fdc_rate_t)(omti->fdc_rate & AP_OMTI_FDC_RATE_MASK);
}

/* §6.3's three command modifiers, from the first byte of the command in
 * progress. Zero-length means no command, and no modifiers with it. */
static bool fdc_modifier(const ap_omti_t *omti, uint8_t bit) {
  return omti->fdc_command_length > 0u && (omti->fdc_command[0] & bit) != 0u;
}

bool ap_omti_fdc_multitrack(const ap_omti_t *omti) {
  return fdc_modifier(omti, AP_OMTI_FDC_MT);
}

bool ap_omti_fdc_mfm(const ap_omti_t *omti) {
  return fdc_modifier(omti, AP_OMTI_FDC_MF);
}

bool ap_omti_fdc_skip_deleted(const ap_omti_t *omti) {
  return fdc_modifier(omti, AP_OMTI_FDC_SK);
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

/* How long the heads take to cross `from` cylinders to `to`.
 *
 * `008778-03` Table 7-7: one step per track at 3 ms minimum, and less than 15 ms
 * to settle "excluding track-to-track time" -- so the settle is charged once, at
 * the end, and only when the heads actually moved. A seek to the cylinder the
 * head is already on costs nothing, which is the one case a step-and-settle
 * model must not charge for.
 *
 * See the header for the check that this composition reproduces Table 7-7's own
 * published 94 ms average. */
static ap_time_t fdc_seek_duration(uint8_t from, uint8_t to) {
  const unsigned distance = from > to ? (unsigned)(from - to)
                                      : (unsigned)(to - from);
  if (distance == 0u) {
    return 0u;
  }
  return (ap_time_t)distance * AP_OMTI_FDC_TRACK_TO_TRACK +
         AP_OMTI_FDC_SETTLING;
}

/* Start a drive moving, and let the Main Status Register say so.
 *
 * The controller itself is free the moment the command is accepted -- §6.3 gives
 * `SEEK` and `RECALIBRATE` no result phase -- so this sets no controller
 * deadline. What it sets is the *drive's*, and `ap_omti_advance` is what marks
 * the arrival. A seek of zero distance arrives at once, so the driver's next
 * `SENSE INTERRUPT STATUS` finds it done, which is what the hardware does with
 * a head already on the requested cylinder. */
static void fdc_begin_seek(ap_omti_t *omti, unsigned unit, uint8_t to) {
  const ap_time_t duration = fdc_seek_duration(omti->fdc_cylinder[unit], to);
  omti->fdc_cylinder[unit] = to;
  if (duration == 0u) {
    omti->fdc_seek_done = true;
    omti->fdc_seek_st0 = (uint8_t)(AP_OMTI_ST0_IC_NORMAL |
                                   AP_OMTI_ST0_SEEK_END | (uint8_t)unit);
    return;
  }
  omti->fdc_seek_at[unit] = omti->now + duration;
}

/* Whether a drive is still moving, which is the Main Status Register's per-drive
 * "in the Seek mode" bit. */
static bool fdc_seeking(const ap_omti_t *omti, unsigned unit) {
  return omti->fdc_seek_at[unit] != AP_TIME_NEVER;
}

/* How long the command in `fdc_command` keeps the *controller* busy.
 *
 * Zero for everything answered out of the controller's own registers --
 * `SENSE DRIVE STATUS`, `SENSE INTERRUPT STATUS`, `SPECIFY`, and the invalid
 * opcode -- on the same division the fixed disk's `command_duration` draws, and
 * for the same reason: nothing touched a surface, so there is nothing to wait
 * for and charging would be inventing time.
 *
 * No seek component. The 765 does not position implicitly: the head is where a
 * prior `SEEK` left it, and `fdc_begin_seek` has already charged that. What is
 * left is the wait for the sector to come round and the time its bytes take to
 * cross the head. */
static ap_time_t fdc_command_duration(const ap_omti_t *omti) {
  unsigned sectors = 0u;
  switch ((uint8_t)(omti->fdc_command[0] & AP_OMTI_FDC_OPCODE_MASK)) {
  case AP_OMTI_FDC_READ_DATA:
  case AP_OMTI_FDC_SCAN_EQUAL:
  case AP_OMTI_FDC_SCAN_LOW_EQUAL:
  case AP_OMTI_FDC_SCAN_HIGH_EQUAL:
    /* One sector through the buffer, whatever EOT names: this model moves them
     * one at a time, exactly as the fixed disk's data path does. §6.3's set has
     * no WRITE DATA and no READ ID, so those are not cases here -- see the
     * header's command enum for why nothing is invented from 765 knowledge. */
    sectors = 1u;
    break;
  case AP_OMTI_FDC_FORMAT_TRACK:
    /* A whole track written, `SC` sectors of it, and it starts at the index
     * hole -- so the wait is for the index rather than for a sector, which
     * averages the same half revolution. */
    sectors = omti->fdc_command[3];
    break;
  default:
    return 0u;
  }
  const uint64_t bytes = (uint64_t)sectors * AP_AFD_SECTOR_BYTES;
  return AP_OMTI_FDC_AVERAGE_LATENCY +
         (ap_time_t)((uint64_t)AP_TIME_BASE_HZ * bytes /
                     AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC);
}

/* The result phase the deadline was standing in front of. */
static void fdc_complete(ap_omti_t *omti) {
  omti->fdc_completion_at = 0u;
  if (omti->fdc_result_length == 0u) {
    fdc_idle(omti);
    return;
  }
  omti->fdc_phase = AP_OMTI_PHASE_STATUS;
  omti->fdc_result_index = 0u;
  /* Controller to host, and busy until the last byte is taken. */
  omti->fdc_status = AP_OMTI_MSR_RQM | AP_OMTI_MSR_DIO | AP_OMTI_MSR_BUSY;
}

/* Enter the result phase, or go straight back to idle when §6.3 gives the
 * command no result bytes -- after the drive has taken as long over it as
 * `008778-03` chapter 7 says it does.
 *
 * `[OMTI]` §4.5 is the shape being modelled: "The controller then goes 'Busy'
 * and executes the command. Upon completion of the command the controller
 * becomes 'not busy' and results may be obtained." So while the deadline stands
 * the Main Status Register reads busy with `RQM` down -- there is no byte to
 * move in either direction -- and the result bytes, already prepared by the
 * caller, are not offered yet. */
static void fdc_result(ap_omti_t *omti) {
  const ap_time_t duration = fdc_command_duration(omti);
  if (duration == 0u) {
    fdc_complete(omti);
    return;
  }
  omti->fdc_phase = AP_OMTI_PHASE_EXECUTING;
  omti->fdc_completion_at = omti->now + duration;
  omti->fdc_status = AP_OMTI_MSR_BUSY;
}

/* The seven-byte result the data commands share. C, H, R and N come back as the
 * position *after* the operation, which is what a driver chains from. */
static void fdc_data_result(ap_omti_t *omti, uint8_t st0, uint8_t st1,
                            uint8_t st2) {
  /* `ST0[2]`, the head address: "the state of the head at the end of the
   * execution phase", which for these commands is the side the command named.
   * See the header -- this was absent, so every result said side 0. */
  const uint8_t head =
      (omti->fdc_command[3] & 1u) ? AP_OMTI_ST0_HEAD : (uint8_t)0u;
  omti->fdc_result[0] = (uint8_t)(st0 | head | (uint8_t)fdc_unit(omti));
  omti->fdc_result[1] = st1;
  omti->fdc_result[2] = st2;
  omti->fdc_result[3] = omti->fdc_command[2]; /* C */
  omti->fdc_result[4] = omti->fdc_command[3]; /* H */
  omti->fdc_result[5] = omti->fdc_command[4]; /* R */
  omti->fdc_result[6] = omti->fdc_command[5]; /* N */
  omti->fdc_result_length = 7u;
}

/* `ST0[3]`, `NR`: p. 8-13's "Set if FDD Not Ready". A drive with no diskette in
 * it is not ready, and that is a property of the drive rather than of the
 * sector a command happened to name. */
static uint8_t fdc_not_ready(const ap_omti_t *omti) {
  return omti->floppy == nullptr ? AP_OMTI_ST0_NOT_READY : (uint8_t)0u;
}

/* Read the sector the command's C/H/R name into the buffer. */
static bool fdc_load_sector(ap_omti_t *omti, uint8_t *st1) {
  uint32_t lba = 0u;
  *st1 = 0u;
  if (omti->floppy == nullptr) {
    /* An empty drive: the sector is not there to be found, and the *drive* is
     * what is wrong rather than the sector. The caller adds `ST0`'s `NR` --
     * p. 8-13's "Set if FDD Not Ready" -- which this reported in `ST1` alone,
     * so an absent drive looked exactly like a seek into unformatted media. */
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
      fdc_data_result(omti, (uint8_t)(AP_OMTI_ST0_IC_ABRUPT | fdc_not_ready(omti)),
                      st1, 0u);
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
      /* Write-protect says why a *loaded* diskette refuses; `NR` says the drive
       * is empty. A format into an empty drive is both, and reporting only the
       * first would tell a driver to swap the write-protect tab on a diskette
       * that is not there. */
      fdc_data_result(omti,
                      (uint8_t)(AP_OMTI_ST0_IC_ABRUPT | fdc_not_ready(omti)),
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
      fdc_data_result(omti, (uint8_t)(AP_OMTI_ST0_IC_ABRUPT | fdc_not_ready(omti)),
                      st1, 0u);
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
     * there reports, and this one always does. It steps, so it costs one step
     * per cylinder from wherever the head was. */
    fdc_begin_seek(omti, unit, 0u);
    fdc_result(omti);
    return;

  case AP_OMTI_FDC_SEEK:
    /* §6.3.10's NCN, which is where the head is going. */
    fdc_begin_seek(omti, unit, omti->fdc_command[2]);
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
     * the drive actually is, and write protect from the image.
     *
     * **The unit was being reported here and must not be.** This built the byte
     * as `ALWAYS | unit`, which is neither manual's register: §6.4.4 has bits 1
     * and 0 constant, and `002398-04` p. 12-14 has them as `UN1`/`UN0` with no
     * constant anywhere. The two readings were mixed, so a `SENSE DRIVE STATUS`
     * for drive B answered `03` -- the part's constant and the handbook's unit
     * field at once, a value neither document describes. The header sets out
     * which reading is followed and what would settle it; whichever wins, this
     * byte is not both. */
    omti->fdc_result[0] = AP_OMTI_ST3_ALWAYS;
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
  case AP_OMTI_FDC_MSR: {
    /* Table 4-3's `NDMA`, bit 5: "non-DMA mode, execution phase only". It is
     * how a driver in programmed I/O knows to move the bytes itself, and it was
     * defined and never set -- so a polled driver saw the same register as a
     * DMA one and had nothing to distinguish them.
     *
     * Execution phase is the data phase, and "non-DMA" is the Digital Output
     * Register's interrupt/DMA enable being clear -- the same bit that gates
     * `IRQ6` and `DRQ2`. One switch, three consumers. */
    uint8_t status = omti->fdc_status;
    if ((omti->fdc_phase == AP_OMTI_PHASE_DATA_IN ||
         omti->fdc_phase == AP_OMTI_PHASE_DATA_OUT) &&
        (omti->dor & AP_OMTI_DOR_INT_DMA) == 0u) {
      status |= AP_OMTI_MSR_NDMA;
    }
    /* Bits 0 and 1, "Drive A/B is in the Seek mode when 1". Composed here
     * rather than stored in `fdc_status`, because a drive can be seeking while
     * the controller is idle, busy on the other drive, or in a data phase --
     * §6.3 releases the controller the moment a `SEEK` is accepted -- and a
     * stored bit would have to be maintained at every one of those transitions.
     * The deadline is the single source. */
    if (fdc_seeking(omti, 0u)) {
      status |= AP_OMTI_MSR_SEEK_A;
    }
    if (fdc_seeking(omti, 1u)) {
      status |= AP_OMTI_MSR_SEEK_B;
    }
    return status;
  }
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
      /* And every deadline the floppy half was working towards. Abandoned
       * rather than left standing, for the reason `ap_omti_disk_reset` gives
       * for the Winchester's: the deadlines are hashed, so two controllers held
       * in reset must be the same machine however they got there. A seek left
       * outstanding would also arrive later and set `fdc_seek_done` on a
       * controller that has forgotten it ever issued one. */
      omti->fdc_completion_at = 0u;
      omti->fdc_seek_at[0] = AP_TIME_NEVER;
      omti->fdc_seek_at[1] = AP_TIME_NEVER;
      omti->fdc_seek_done = false;
      omti->fdc_seek_st0 = 0u;
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
    omti->fdc_control = value;
    return;
  case AP_OMTI_FDC_DIR:
    /* Table 4-3's Diskette Control Register, which is a different register from
     * the one above and used to be the same byte. See `fdc_rate`. */
    omti->fdc_rate = value;
    return;
  default:
    return;
  }
}
