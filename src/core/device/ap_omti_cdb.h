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
/* `002398-04` p. 12-11 lists the DN3000's disk op codes and agrees with §5.1.2
 * on twenty of the twenty-two it prints. The two it does not:
 *
 *     Assign Alternate Track    handbook $10   §5.1.2 `11`
 *     Read to Sector Buffer     handbook $30   §5.1.2 `1E`
 *
 * `10` in §5.1.2 is CHECK TRACK FORMAT, so the first would collide; the second
 * names no code the manual lists at all. §5.1.2's are used, and the second is
 * settled by measurement rather than by preference: **Domain/OS issues `1E`**,
 * paired with `0E`, and this core's own boot records it -- `1E` being
 * unimplemented is what crashed the machine (see `ap_omti.c`'s
 * `AP_OMTI_CMD_READ_TO_BUFFER`). The handbook's list is what a driver's header
 * file says, not what the controller decodes. */
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

/* §5.4.29's ten-byte reply.
 *
 * The three "(-1)" fields are the trap: the manual marks cylinders, heads and
 * sectors as one *less* than the count -- the highest valid number rather than
 * how many there are -- so a model returning the counts describes a drive one
 * cylinder, one head and one sector larger than it has.
 *
 * ## The reply is the **soft** sectored one, and the drive says so itself
 *
 * §5.4.29 prints three tables: bytes 0-6 for "HARD and SOFT SECTORED Drives",
 * then bytes 7-9 twice, once for each. Which of the two applies is not a choice
 * -- it is stated by byte 5, the low half of the drive configuration word,
 * whose own table on page 5-27 gives bit 2 as "ESDI SOFT SECTORED" and bit 1 as
 * "ESDI HARD SECTORED".
 *
 * This core returns `02 44` for that word, and `0x44` is bits 6 and 2: **ESDI
 * FIXED MEDIA** and **ESDI SOFT SECTORED**. So bytes 7, 8 and 9 are the soft
 * sectored set -- PLO sync (ID), PLO sync (DATA), ISG after sector -- and this
 * comment previously named the hard sectored layout, which the drive it
 * describes is not. No byte changes, because all three are zero either way;
 * what changes is whether the file says something true about them.
 *
 * That word was taken from the oracle when nothing here could read it. Page
 * 5-27 defines it bit by bit, and the two now agree: `0x02` in byte 4 is bit 1,
 * "TRANSFER RATE T = 10 MHZ, Supported", which is an ESDI drive's rate.
 *
 * Bytes 6 to 9 -- the inter-sector gaps and the PLO sync fields -- are zero.
 * They are physical formatting parameters a raw sector image has no way to
 * carry, and the oracle writes zeros for them too. A stated gap, not a value. */
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
 * settable where an ESDI drive reports it with READ CAPACITY.
 *
 * §2.3's four jumper tables corroborate the split from the other end. Each of
 * the 8620, 8120, 8627 and 8127 tables gives two configuration bits per LUN and
 * four drive types, and **every jumperable type carries a `#CYL` and `#HEADS`
 * except the ESDI row, which is blank in both columns** -- on the two ESDI
 * controllers, and on the two ST506 ones that encoding is `RESERVED` and there
 * is no ESDI row at all. Geometry is strapped for ST506 and interrogated for
 * ESDI, which is the same boundary this define sits on. */
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
  uint8_t control;       /* byte 5 whole; see the masks below */
} ap_omti_cdb_t;

/* Decode a six-byte CDB. Always succeeds -- a CDB is a fixed layout, and whether
 * the *command* is one this controller accepts is a separate question. */
/* §5.2's CONTROL BYTE, and a contradiction between two sections of the same
 * manual that this file previously resolved the wrong way.
 *
 * §5.1.1's byte-by-byte summary says of byte 5: "Bits 7,6,5 contain the command
 * Control Byte. Bits 4,3,2,1,0 are not used." §5.2 then lays the byte out in
 * full and gives **bits 2, 1 and 0 as the STEP option**, with p. 5-4's table of
 * eight step rates from "3 milliseconds per step" to "10 microseconds,
 * buffered step". Both are on the page images; the summary is the loose one.
 *
 * This decoded `(bytes[5] >> 5) & 0x07` and threw the step field away, with a
 * test named `..._is_three_bits` asserting the loss. The whole byte is kept
 * now and the fields are named.
 *
 *     Bit: | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *          | R |E/B| C | x | x | S | S | S |
 */
#define AP_OMTI_CONTROL_DISABLE_RETRY 0x80u
/* Bit 6 is two options on one bit, by command: "DISABLE ECC" on a READ and
 * "ENABLE FORMAT BUFFER" on a FORMAT. */
#define AP_OMTI_CONTROL_DISABLE_ECC 0x40u
#define AP_OMTI_CONTROL_FORMAT_BUFFER 0x40u
#define AP_OMTI_CONTROL_ADDRESS_CONVERSION 0x20u

/* The conversion geometry of §5.2: sixteen heads per cylinder always, and the
 * sectors per track the board's jumpers select. See the header for how 18 is
 * established from the drive geometries rather than chosen. */
#define AP_OMTI_CONVERSION_HEADS 16u
#define AP_OMTI_CONVERSION_SECTORS 18u
#define AP_OMTI_CONTROL_STEP 0x07u

/* ## Which of the four options this controller acts on, and why the rest are
 * inert rather than forgotten
 *
 * `FORMAT BUFFER` (bit 6 on a FORMAT) **is acted on**: set, the data buffer
 * fills each data field; clear, §5.2's `6C` pattern is written instead, which
 * `ap_omti.c`'s `format_track` does.
 *
 * The other three are decoded and deliberately not acted on. Each is inert for
 * a stated reason, not by omission:
 *
 * - **`DISABLE RETRY` (bit 7)** selects "4 retries, 1 recalibration, 4 retries"
 *   against an error. This model has no medium that fails -- a read either
 *   addresses a sector the image holds or is refused -- so there is no error to
 *   retry and both settings behave alike. It becomes observable when media
 *   errors can be injected.
 * - **`DISABLE ECC` (bit 6 on a READ)** chooses whether a correctable ECC error
 *   is corrected or reported. Same reason: nothing here produces one. The ECC
 *   *bytes* are modelled (`ap_awd_ecc`), the failure is not.
 * - **`ENABLE SECTOR ADDRESS CONVERSION` (bit 5)** is modelled. §5.2: "the
 *   controller will perform a sector address conversion based on 16 heads per
 *   cylinder. The number of sectors per track used in the conversion is based
 *   on the SECTORS PER TRACK Jumpers". The address in the CDB is therefore in a
 *   host geometry of sixteen heads and the jumpered sectors, and the controller
 *   re-expresses it in the drive's own; a linear block is what the two share.
 *
 *   **The jumper count is settled, and the manual disagrees with itself about
 *   which jumpers they are.** §5.2 says "W10 and W9"; the jumper table under
 *   COMMON SYSTEM JUMPER SETTINGS on page 2-8 says **W10 and W11** and is the
 *   one that carries values:
 *
 *       W10 W11   bytes/sector   sectors/track (ST506/412 MFM)
 *        0*  0        512             17
 *        0   1        512             18
 *        1   0       1024              9
 *        1   1       1056              9
 *
 *   This board is **18 sectors per track**, and that comes from two independent
 *   places rather than from a choice made here. `image/ap_awd.h` records both
 *   Apollo drives from their own geometry -- the 348 MB Maxtor EXT-4380-E at
 *   1223 cylinders, 15 heads, **18 sectors**, and the 155 MB Micropolis 1355 at
 *   1023, 8, **18** -- and 18 is
 *   the only entry in the table above that produces it, at 512 bytes per sector
 *   which is what the images are. The same page's `W19/W18/W17` gives the
 *   Winchester base address as `01A0h` in one of its eight rows, which is where
 *   this board decodes the controller, so the table describes this strapping in
 *   two places.
 *
 *   The Domain/OS boot measured here never sets the bit, so nothing observed
 *   depends on this; it is implemented because the document defines it.
 * - **`STEP` (bits 2-0)** picks one of eight step rates, from 3 milliseconds
 *   per step to 10 microseconds buffered.
 *
 *   `002398-04` p. 12-10 prints the same eight rows for the DN3000 and calls the
 *   field "**drive type and step option**", which is a second document agreeing
 *   value for value -- 25, 50, 200 and 70 microseconds at `010`, `011`, `100`
 *   and `101`, and 3 milliseconds at `000`, `110` and `111`. It differs in one
 *   row and the difference is Apollo's rather than a contradiction: `001`, which
 *   `[OMTI]` gives as "10 microseconds, buffered step", is marked **N/A** --
 *   a rate none of this machine's approved drives takes. It also names `000`
 *   "unknown drive", which is what a 3 ms default is for.
 *
 *   Seeks in this model complete inside
 *   the command that issues them -- the same reason `MSR_SEEK_A`/`_B` are
 *   documented as never set -- so no step rate is observable. It becomes real
 *   when seeks take time, and the field is decoded now so that the day it does,
 *   the value is already there. */

void ap_omti_cdb_decode(const uint8_t *bytes, ap_omti_cdb_t *out);

/* Whether a command makes the drive move, and so whether it costs access time.
 *
 * The division is physical, not a list of opcodes that happen to be slow:
 * everything the controller answers out of its own registers or its sector
 * buffer touches no surface and completes at once, and everything that
 * positions the heads waits for a seek and for the sector to come round. A
 * command absent from this predicate is one the model asserts is register-local
 * -- so `READ SECTOR BUFFER` and `READ CAPACITY` are deliberately outside it,
 * and `SEEK` and `RECALIBRATE` deliberately inside.
 *
 * Why it matters beyond precision: a zero access time crashes Domain/OS. See
 * `ap_omti.h`'s completion fields. */
[[nodiscard]] bool ap_omti_cdb_touches_surface(uint8_t command);

/* Whether an ESDI controller accepts this command. False for the ST506-only
 * command and for anything not in §5.1.2.
 *
 * **That this machine's drive is ESDI is now settled from Apollo's side, not
 * only from `[OMTI]`'s.** `008778-03` chapter 5 describes the controller's
 * cabling as ST506/412 throughout -- §5.3.4.2's J3/J4 carry MFM read and write
 * data, and §5.4.1.2 has the five OMTI chips controlling "one or two ST506/412
 * type hard disks" -- which reads like a flat contradiction of an ESDI command
 * set. §6.3 resolves it by drive rather than by controller: "The 72-MB drive,
 * used only in the DS3000, should be ... compatible with the industry-standard
 * **ST412** rigid disk drive interface, transferring MFM data at 5.0
 * megabits/second. The **155-MB and 348-MB** drives should be ... compatible
 * with the industry-standard **ESDI** rigid disk drive interfaces, transferring
 * NRZ data at 10.0 megabits/second." Chapter 5 is describing the 72 Mbyte
 * drive's wiring, which is the only one a DS3000 takes; the drives a DS4000
 * carries -- and the class this core's image is -- are ESDI.
 *
 * It follows that §5.4.5's power-on drive parameters (4 heads, maximum cylinder
 * 306, reduced write current and write precompensation both at cylinder 153)
 * are **not a gap in this model**. They are defaults for the parameter list of
 * `0C INITIALIZE DRIVE CHARACTERISTICS`, the one command this predicate
 * refuses, so on an ESDI controller nothing ever reads them. They were recorded
 * as an open gap while the interface was unsettled; §6.3 closes it. */
[[nodiscard]] bool ap_omti_cdb_accepted_by_esdi(uint8_t command);

/* How many bytes the command's descriptor block occupies: ten for COPY, six for
 * everything else. */
[[nodiscard]] unsigned ap_omti_cdb_length(uint8_t command);

#endif /* APOLLO_DEVICE_AP_OMTI_CDB_H */
