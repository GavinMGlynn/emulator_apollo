/* OMTI Command Descriptor Blocks: `[OMTI]` §5.1.
 *
 * "The processor specifies the operation or command to be executed by the
 * controller by sending 6 or 10 bytes called a Command Descriptor Block (CDB)."
 *
 * ## The cylinder is split across three bytes
 *
 * Byte 1's bit 7 carries C10, byte 2's bits 7 and 6 carry C09 and C08, and byte
 * 3 carries the low eight. Eleven bits in all.
 *
 * A decoder taking the cylinder from byte 3 alone works perfectly on any disk
 * under 256 cylinders and fails on every real one -- which is the shape of bug
 * that survives every small test anyone writes, so the reassembly is the thing
 * this module exists to get right.
 *
 * ## Command codes are a whole byte, and also two fields
 *
 * §5.1.1 splits byte 0 into "bits 7,6 and 5 identify the class of the command"
 * and "bits 4,3,2,1, 0 contain the command Opcode", while §5.1.2's summary lists
 * codes as whole bytes up to `E6`. Both are true: the byte is the command, and
 * its top three bits are the class. Exposing only the five-bit opcode would make
 * `08 READ` and `E8` indistinguishable.
 */

#ifndef APOLLO_DEVICE_AP_OMTI_CDB_H
#define APOLLO_DEVICE_AP_OMTI_CDB_H

#include <stdbool.h>
#include <stdint.h>

#define AP_OMTI_CDB_SHORT 6u
#define AP_OMTI_CDB_LONG 10u /* COPY only */

/* §5.1.2, the commands common to all models. */
#define AP_OMTI_CMD_TEST_DRIVE_READY 0x00u
#define AP_OMTI_CMD_RECALIBRATE 0x01u
#define AP_OMTI_CMD_REQUEST_SENSE 0x03u
#define AP_OMTI_CMD_FORMAT_DRIVE 0x04u
#define AP_OMTI_CMD_READ_VERIFY 0x05u
#define AP_OMTI_CMD_FORMAT_TRACK 0x06u
#define AP_OMTI_CMD_FORMAT_BAD_TRACK 0x07u
#define AP_OMTI_CMD_READ 0x08u
#define AP_OMTI_CMD_WRITE 0x0Au
#define AP_OMTI_CMD_SEEK 0x0Bu
#define AP_OMTI_CMD_READ_ECC_LENGTH 0x0Du
#define AP_OMTI_CMD_READ_SECTOR_BUFFER 0x0Eu
#define AP_OMTI_CMD_WRITE_SECTOR_BUFFER 0x0Fu
#define AP_OMTI_CMD_ASSIGN_ALTERNATE 0x11u
/* §5.4.17 START/STOP, "Valid for ESDI drives only" -- which this core did not
 * accept at all until §5.4 was read end to end. Byte 4 bit 0 is START. */
#define AP_OMTI_CMD_START_STOP 0x1Au
#define AP_OMTI_CMD_CHANGE_CARTRIDGE 0x1Bu
#define AP_OMTI_CMD_READ_TO_BUFFER 0x1Eu
#define AP_OMTI_CMD_WRITE_FROM_BUFFER 0x1Fu
#define AP_OMTI_CMD_COPY 0x20u
#define AP_OMTI_CMD_RAM_DIAGNOSTICS 0xE0u
#define AP_OMTI_CMD_READ_ID 0xE2u
#define AP_OMTI_CMD_DRIVE_DIAGNOSTIC 0xE3u
#define AP_OMTI_CMD_CONTROLLER_DIAGNOSTIC 0xE4u
#define AP_OMTI_CMD_READ_LONG 0xE5u
#define AP_OMTI_CMD_WRITE_LONG 0xE6u

/* §5.1.2, "COMMANDS SPECIFIC to the ESDI drives" -- which is what the DN3500
 * carries. */
#define AP_OMTI_CMD_CHECK_TRACK_FORMAT 0x10u
#define AP_OMTI_CMD_READ_ESDI_DEFECT_LIST 0x37u
/* §5.1.2's summary calls `EC` "READ CAPACITY"; §5.4.29 -- the command's own
 * description, in the same manual -- calls it **READ CONFIGURATION**. Same
 * code, two names. The description's is taken because it is the one that says
 * what the ten data bytes are, and both are recorded so a reader who finds the
 * other is not left wondering which command this is. */
#define AP_OMTI_CMD_READ_CAPACITY 0xECu
#define AP_OMTI_CMD_READ_CONFIGURATION AP_OMTI_CMD_READ_CAPACITY

/* §5.4.29's ten-byte reply, for a **hard sectored** drive.
 *
 * The three "(-1)" fields are the trap: the manual marks cylinders, heads and
 * sectors as one *less* than the count -- the highest valid number rather than
 * how many there are -- so a model returning the counts describes a drive one
 * cylinder, one head and one sector larger than it has.
 *
 * Bytes 4 to 9 are physical formatting parameters -- the drive configuration
 * word, the inter-sector gaps and the PLO sync fields -- which no manual in
 * `docs/references/` gives for this drive and which a raw sector image has no
 * way to carry. They are zero, and that is a stated gap rather than a value. */
#define AP_OMTI_CONFIGURATION_BYTES 10u

/* §5.4.24's READ ID reply: four bytes, two words.
 *
 *     0  zero  zero  zero  zero  zero  C10  C09  C08
 *     1  CYLINDER LOW (C07 to C00)
 *     2  FLAGS  |  0  |  HEAD NUMBER
 *     3  SECTOR NUMBER
 *
 * The flags are page 5-23's, and they describe the *track* rather than the
 * transfer: an alternate track, a bad track with an alternate assigned, and a
 * bad track. A raw sector image has no bad tracks and no alternates -- it is a
 * perfect disk by construction -- so all three are clear and that is a fact
 * about the image format, not an assumption about the drive. */
#define AP_OMTI_READ_ID_BYTES 4u
#define AP_OMTI_ID_FLAG_ALTERNATE 0x20u
#define AP_OMTI_ID_FLAG_BAD_WITH_ALTERNATE 0x40u
#define AP_OMTI_ID_FLAG_BAD 0x80u

/* §5.1.2, "COMMANDS SPECIFIC to the ST506/412 drives". Named so it can be
 * *rejected* on an ESDI controller rather than quietly accepted: the two lists
 * sit adjacent on the page, and merging them would make drive geometry look
 * settable where an ESDI drive reports it with READ CAPACITY. */
#define AP_OMTI_CMD_INITIALIZE_DRIVE_CHARACTERISTICS 0x0Cu

typedef struct {
  uint8_t command;       /* byte 0 whole; the code §5.1.2 lists */
  uint8_t command_class; /* byte 0 bits 7-5 */
  uint8_t opcode;        /* byte 0 bits 4-0 */
  uint8_t lun;           /* byte 1 bit 5 */
  uint8_t head;          /* byte 1 bits 4-0 */
  uint16_t cylinder;     /* C10..C00, reassembled from bytes 1, 2 and 3 */
  uint8_t sector;        /* byte 2 bits 5-0 */
  uint8_t block_count;   /* byte 4; the interleave factor for FORMAT */
  uint8_t control;       /* byte 5 bits 7-5 */
} ap_omti_cdb_t;

/* Decode a six-byte CDB. Always succeeds -- a CDB is a fixed layout, and whether
 * the *command* is one this controller accepts is a separate question. */
void ap_omti_cdb_decode(const uint8_t *bytes, ap_omti_cdb_t *out);

/* Whether an ESDI controller accepts this command. False for the ST506-only
 * command and for anything not in §5.1.2. */
[[nodiscard]] bool ap_omti_cdb_accepted_by_esdi(uint8_t command);

/* How many bytes the command's descriptor block occupies: ten for COPY, six for
 * everything else. */
[[nodiscard]] unsigned ap_omti_cdb_length(uint8_t command);

#endif /* APOLLO_DEVICE_AP_OMTI_CDB_H */
