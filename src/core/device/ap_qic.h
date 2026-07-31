/* QIC-02 cartridge tape drive.
 *
 * The command layer behind the SC-499 controller: `[SC499]` §1.13, "The SC-499
 * controller is designed to accept the QIC-02 command set." The controller's
 * registers are `device/ap_sc499.h`; this is what its commands mean.
 *
 * ## The cartridge type is told, not derived
 *
 * `[SC499]` §1.13: the controller "shall discriminate between DC300XL and DC600A
 * cartridges by measurement of BOT to LOAD POINT distance". It identifies the
 * cartridge from tape *geometry* -- and a `.ct` image is a raw block image with
 * no geometry at all (`FINDINGS.md` C24). There is nothing in the file to
 * measure.
 *
 * So `ap_qic_load` takes the type from its caller. That is a fact about this
 * emulation's inputs rather than about the hardware, and naming it here is
 * cheaper than a format-detection routine failing mysteriously later.
 *
 * ## Two commands are refused because their codes are unknown
 *
 * ERASE and SELECT Q11 FORMAT are in `[SC499]` §1.13.1's list, and the scan lost
 * their opcodes to handwritten annotation (`FINDINGS.md` C25). Both are
 * constrained to the `2x` group but constrained is not known, so no code is
 * claimed for either -- an unrecognised command is refused, and if one of the
 * missing pair is ever issued it will be refused rather than silently doing
 * something else.
 *
 * ## Writing is not modelled
 *
 * WRITE and WRITE FILE MARK are recognised and refused. The images this core
 * reads are distribution cartridges opened read-only, and there is no write-back
 * path; accepting a write and discarding it would let an installation appear to
 * succeed.
 */

#ifndef APOLLO_DEVICE_AP_QIC_H
#define APOLLO_DEVICE_AP_QIC_H

#include <stdbool.h>
#include <stdint.h>

#include "image/ap_ct.h"

/* `[SC499]` §1.13.1, as far as the scan is legible. ERASE and SELECT Q11 FORMAT
 * are deliberately absent; see the header. */
typedef enum {
  AP_QIC_CMD_SELECT = 0x01u,        /* "0000 0001", soft lock off */
  AP_QIC_CMD_SELECT_LOCK = 0x11u,   /* "0001 0001", soft lock on */
  AP_QIC_CMD_BOT = 0x21u,           /* "0010 0001" */
  AP_QIC_CMD_RETENSION = 0x24u,     /* "0010 0100" */
  AP_QIC_CMD_SELECT_Q24 = 0x27u,
  AP_QIC_CMD_WRITE = 0x40u,
  AP_QIC_CMD_WRITE_FILE_MARK = 0x60u,
  AP_QIC_CMD_READ = 0x80u,
  AP_QIC_CMD_READ_FILE_MARK = 0xA0u,
  AP_QIC_CMD_READ_STATUS = 0xC0u,
} ap_qic_command_t;

/* The two cartridges `[SC499]` names. Supplied by the caller. */
typedef enum {
  AP_QIC_CARTRIDGE_NONE = 0,
  AP_QIC_CARTRIDGE_DC300XL,
  AP_QIC_CARTRIDGE_DC600A,
} ap_qic_cartridge_t;

typedef struct {
  ap_ct_t image;
  bool loaded;
  ap_qic_cartridge_t cartridge;

  /* "The drive shall remain selected until changed by another SELECT command or
   * RESET" -- state, not a momentary action. */
  bool selected;
  bool soft_lock;
  bool q24_format;

  uint64_t position; /* next block to be read */
  bool reading;      /* a READ command is in progress */
} ap_qic_t;

void ap_qic_reset(ap_qic_t *qic);

/* Load a cartridge. `cartridge` is the type, which the drive cannot derive; see
 * the header. Fails if the image is not a whole number of blocks. */
[[nodiscard]] bool ap_qic_load(ap_qic_t *qic, const uint8_t *data, size_t size,
                               ap_qic_cartridge_t cartridge);
void ap_qic_eject(ap_qic_t *qic);

/* Issue a command. False for a command this core does not model or does not
 * recognise -- including the two whose opcodes the scan lost, which cannot be
 * recognised because no code is claimed for them. */
[[nodiscard]] bool ap_qic_command(ap_qic_t *qic, uint8_t command);

/* Read the next block of the cartridge. Requires a READ command to have been
 * issued and a cartridge to be loaded and selected. */
[[nodiscard]] bool ap_qic_read_block(ap_qic_t *qic, uint8_t *out);

/* Whether a command code is one this core models at all. */
[[nodiscard]] bool ap_qic_command_known(uint8_t command);

#endif /* APOLLO_DEVICE_AP_QIC_H */
