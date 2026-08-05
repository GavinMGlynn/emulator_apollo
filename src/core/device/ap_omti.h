/* OMTI 862X ESDI/floppy controller.
 *
 * `[OMTI]` *OMTI IBM PC AT Controller Series Reference Manual*, Scientific Micro
 * Systems, January 1987, publication 3001483. §4.1 addresses the **862X**
 * family, so it covers the DN3500's 8621 although the title page lists only the
 * 8620 and 8627.
 *
 * ## One card, two controllers
 *
 * §4.1: "the OMTI 862X controller looks like two independent controllers - one
 * controller for the floppy disk, and one controller for the fixed disk. The
 * host communicates with the OMTI 8000 series through two independent sets of
 * registers." §3.4 says the hardware matches that view: "This allows full
 * concurrent operations between these two sections. For example, DMA data
 * transfer could be occurring at the same time as programmed Input/Output data
 * transfers are occurring on the fixed disk."
 *
 * So this is modelled as two register sets that share nothing, not as one
 * controller with a mode bit. They are even placed 74 KB apart in Apollo's
 * address space -- measured, `FINDINGS.md` C22.
 *
 * ## What is modelled and what is not
 *
 * The two register sets and their documented read/write asymmetries. **Not** the
 * command sets: `[OMTI]` §5 (fixed disk) and §6 (floppy) describe Command
 * Descriptor Blocks and their protocols, and those want a drive and a disk image
 * behind them. This is the interface a driver programs, in the same sense as the
 * 8237A's register model.
 *
 * ## The trap
 *
 * §4.2's data register is not a fixed width: "This is an 8 or 16 bit register
 * depending on the state of the controller (determined by the C/D bit in the
 * STATUS register) ... When the C/D bit is 1, only bits 0-7 are valid. When C/D
 * is 0 all 16 bits are valid." A model with a fixed-width data register would
 * carry commands correctly and corrupt every data word, or the reverse.
 */

#ifndef APOLLO_DEVICE_AP_OMTI_H
#define APOLLO_DEVICE_AP_OMTI_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_omti_cdb.h"
#include "image/ap_afd.h"
#include "image/ap_awd.h"

/* `[OMTI]` Table 4-1, four ports, different meanings read and written. */
#define AP_OMTI_DISK_REGISTERS 4u
typedef enum {
  AP_OMTI_DISK_DATA = 0u,   /* read DATA IN, write DATA OUT */
  AP_OMTI_DISK_STATUS = 1u, /* read STATUS, write RESET (a function) */
  AP_OMTI_DISK_CONFIG = 2u, /* read CONFIGURATION, write SELECT (a function) */
  AP_OMTI_DISK_MASK = 3u,   /* read N/A, write MASK */
} ap_omti_disk_reg_t;

/* `[OMTI]` Table 4-2, the fixed-disk status register. */
#define AP_OMTI_ST_FIXED 0xC0u /* bits 7 and 6, "Not Used (Set to 1)" */
#define AP_OMTI_ST_IREQ 0x20u  /* 1 = Command Complete */
#define AP_OMTI_ST_DREQ 0x10u  /* 1 = DMA Cycle Requested */
#define AP_OMTI_ST_BSY 0x08u   /* 1 = Controller Selected */
#define AP_OMTI_ST_CD 0x04u    /* 1 = byte is a command or status byte */
/* ## The two bits the whole handshake runs on, and they were missing
 *
 * Table 4-2 gives eight bits and this file had six. `I/O` is the *direction* --
 * "0 = from the host to the controller, 1 = from the controller to the Host" --
 * and `REQ` is the request itself: "1 = Request transfer of one byte or Word via
 * Data In or Data Out register".
 *
 * §4.3 turns every phase on `REQ`. The controller sets it to ask for a byte, the
 * host writes or reads one, and *that access* clears it; the pair repeats until
 * the transfer is done. A model without `REQ` has no handshake at all, and a
 * driver polling for it waits for ever -- which is exactly what a `FORCE LOAD`
 * did here, timing out on `STATUS` and then five times on `DATA`. */
#define AP_OMTI_ST_IO 0x02u    /* 1 = controller to host */
/* §4.2's MASK register: "Enables and disables interrupts and DMA transfers."
 * Bits 7-2 are not used. */
#define AP_OMTI_MASK_INTERRUPT_ENABLE 0x02u
#define AP_OMTI_MASK_DMA_ENABLE 0x01u
#define AP_OMTI_ST_REQ 0x01u   /* 1 = transfer one byte or word */

/* `[OMTI]` Table 4-3, the floppy half: five registers within an eight-address
 * block based at AT `3F0`, so the offsets are 2 and 4 through 7. */
#define AP_OMTI_FLOPPY_REGISTERS 8u
typedef enum {
  AP_OMTI_FDC_DOR = 2u,      /* write Digital Output; read N/A */
  AP_OMTI_FDC_MSR = 4u,      /* read Main Status; write N/A */
  AP_OMTI_FDC_DATA = 5u,     /* read and write Data */
  AP_OMTI_FDC_CONTROL = 6u,  /* write Additional Control; read N/A */
  AP_OMTI_FDC_DIR = 7u,      /* read Digital Input; write Diskette Control */
} ap_omti_fdc_reg_t;

/* Digital Output Register, `[OMTI]` Table 4-3. "All bits are cleared when a
 * channel reset occurs." */
#define AP_OMTI_DOR_DRIVE_B_MOTOR 0x20u
#define AP_OMTI_DOR_DRIVE_A_MOTOR 0x10u
#define AP_OMTI_DOR_INT_DMA 0x08u
/* Bit 2 runs the opposite way to every other control bit here: "Reset floppy
 * disk function when 0. The floppy disk function comes out of reset when this
 * bit is set to 1." So clearing the register to stop the motors also asserts
 * reset. */
#define AP_OMTI_DOR_NOT_RESET 0x04u
#define AP_OMTI_DOR_SELECT_B 0x01u /* "A 0 selects drive A, A 1 selects drive B" */

/* Digital Input Register: "Bit 7 ... is received from pin 34 of the floppy disk
 * control cable and is normally used for diskette change status. Bits 0 through
 * 6 are Reserved." */
#define AP_OMTI_DIR_DISK_CHANGE 0x80u

/* The floppy Main Status Register.
 *
 * `[OMTI]` Table 4-3 names these; the sibling 8640 manual's §5.1 spells them
 * out and is the transcription used here, because that manual has a text layer
 * where ours is a scan. Bit 7 "must be used by the host to perform handshaking
 * ... cleared by reading or writing the Data Register", bit 6 gives the
 * direction, bit 4 is busy, bits 1 and 0 report a seek in progress per drive.
 * Bits 3 and 2 are reserved. */
#define AP_OMTI_MSR_RQM 0x80u  /* the data register will move a byte now */
#define AP_OMTI_MSR_DIO 0x40u  /* 1 = controller to host */
#define AP_OMTI_MSR_NDMA 0x20u /* non-DMA mode, execution phase only */
#define AP_OMTI_MSR_BUSY 0x10u /* executing a command */
#define AP_OMTI_MSR_SEEK_B 0x02u
#define AP_OMTI_MSR_SEEK_A 0x01u

/* §6.3's floppy command set: the opcode is the low five bits of the first
 * command byte, the top three being MT, MF and SK on the commands that take
 * them. Read from the page images of `[OMTI]` §6.3, since that section is a
 * scan; the 8640 manual's §5.3 lists the same eleven commands in text and is
 * the independent check on the list.
 *
 * **There is no WRITE DATA command.** Not in our §6.3, and not in the 8640's
 * §5.3 summary either -- both list exactly these ten and INVALID. The ST1 and
 * ST2 bit descriptions *do* mention Write Data and Write Deleted Data, but that
 * is the NEC 765 status prose those registers inherit, not evidence of a
 * command this controller accepts. Nothing here invents one from general 765
 * knowledge: a driver issuing `05` gets the INVALID path, which is what the
 * documented controller does. */
typedef enum {
  AP_OMTI_FDC_SPECIFY = 0x03u,
  AP_OMTI_FDC_SENSE_DRIVE = 0x04u,
  AP_OMTI_FDC_READ_DATA = 0x06u,
  AP_OMTI_FDC_RECALIBRATE = 0x07u,
  AP_OMTI_FDC_SENSE_INTERRUPT = 0x08u,
  AP_OMTI_FDC_FORMAT_TRACK = 0x0Du,
  AP_OMTI_FDC_SEEK = 0x0Fu,
  AP_OMTI_FDC_SCAN_EQUAL = 0x11u,
  AP_OMTI_FDC_SCAN_LOW_EQUAL = 0x19u,
  AP_OMTI_FDC_SCAN_HIGH_EQUAL = 0x1Du,
} ap_omti_fdc_command_t;

/* The opcode field, and the three modifiers above it. */
#define AP_OMTI_FDC_OPCODE_MASK 0x1Fu
#define AP_OMTI_FDC_MT 0x80u /* multitrack */
#define AP_OMTI_FDC_MF 0x40u /* MFM rather than FM */
#define AP_OMTI_FDC_SK 0x20u /* skip deleted-data address mark */

/* ST0. Bits 7-6 are the interrupt code, and the four values are the whole of
 * what a driver checks first. */
#define AP_OMTI_ST0_IC_MASK 0xC0u
#define AP_OMTI_ST0_IC_NORMAL 0x00u  /* completed and properly executed */
#define AP_OMTI_ST0_IC_ABRUPT 0x40u  /* started, not successfully completed */
#define AP_OMTI_ST0_IC_INVALID 0x80u /* never started */
#define AP_OMTI_ST0_IC_NOT_READY 0xC0u /* 'ready' changed state mid-command */
#define AP_OMTI_ST0_SEEK_END 0x20u
#define AP_OMTI_ST0_EQUIPMENT 0x10u /* fault, or no track 0 after 77 steps */
#define AP_OMTI_ST0_UNIT_MASK 0x03u

/* ST1. */
#define AP_OMTI_ST1_END_CYLINDER 0x80u
#define AP_OMTI_ST1_DATA_ERROR 0x20u
#define AP_OMTI_ST1_OVERRUN 0x10u
#define AP_OMTI_ST1_NO_DATA 0x04u
#define AP_OMTI_ST1_NOT_WRITEABLE 0x02u
#define AP_OMTI_ST1_MISSING_MARK 0x01u

/* ST2. */
#define AP_OMTI_ST2_CONTROL_MARK 0x40u
#define AP_OMTI_ST2_DATA_FIELD_ERROR 0x20u
#define AP_OMTI_ST2_WRONG_CYLINDER 0x10u
#define AP_OMTI_ST2_SCAN_HIT 0x08u
#define AP_OMTI_ST2_SCAN_NOT_SATISFIED 0x04u
#define AP_OMTI_ST2_BAD_CYLINDER 0x02u
#define AP_OMTI_ST2_MISSING_DATA_MARK 0x01u

/* ST3. Bit 0 is "always 1", which is the only constant bit in the four.
 *
 * Bit 4's description in the 8640 manual contradicts its own name: "Track 0
 * (TO) - Status of the 'ready' signal from the diskette drive". The name is
 * modelled and the sentence is not, because bit 4 is Track 0 on every 765-family
 * part and a drive-ready bit that moves when the head reaches cylinder 0 would
 * be reported to a driver as readiness it never gained. Recorded rather than
 * quietly resolved: no manual here states the drive-ready bit's position, so a
 * driver polling for ready will not see it change. */
#define AP_OMTI_ST3_WRITE_PROTECT 0x40u
#define AP_OMTI_ST3_TRACK_0 0x10u
#define AP_OMTI_ST3_HEAD 0x04u
#define AP_OMTI_ST3_ALWAYS 0x01u

/* Where the fixed-disk half is in a command.
 *
 * `[OMTI]` §5.1.1: a command is a descriptor block written a byte at a time to
 * the data port, then the data phase, then a completion status byte. Table 4-2's
 * `C/D` bit is what tells a driver which of those it is looking at, and this is
 * the state behind that bit rather than a second account of it. */
typedef enum {
  AP_OMTI_PHASE_IDLE,
  AP_OMTI_PHASE_COMMAND, /* accumulating the descriptor block */
  AP_OMTI_PHASE_DATA_IN, /* the controller has bytes for the host */
  AP_OMTI_PHASE_DATA_OUT, /* the host is sending bytes */
  AP_OMTI_PHASE_STATUS,  /* the completion byte is waiting */
} ap_omti_phase_t;

/* One sector, and the largest transfer a single command moves here. §5.1.2's
 * block count is a byte, so a command can ask for 255 sectors; this moves them
 * one at a time through the same buffer, which is what the controller's own
 * single sector buffer does. */
#define AP_OMTI_BUFFER_BYTES AP_AWD_SECTOR_BYTES

/* The longest floppy command and result phases in §6.3. READ DATA and the three
 * scans take nine command bytes; the seven-byte ST0/ST1/ST2/C/H/R/N result is
 * the longest going the other way. */
#define AP_OMTI_FDC_COMMAND_MAX 9u
#define AP_OMTI_FDC_RESULT_MAX 7u

typedef struct {
  /* Fixed disk. */
  uint16_t data;
  uint8_t status;
  uint8_t configuration;
  uint8_t mask;

  /* Floppy, entirely separate. */
  uint8_t dor;
  uint8_t fdc_status;
  uint8_t fdc_data;
  uint8_t fdc_control;
  bool disk_change;

  /* The floppy's own command phase, which shares nothing with the fixed disk's:
   * §4.1 has the two halves independent and §3.4 has them running at the same
   * time, so a single phase variable would make a floppy seek cancel a disk
   * read. */
  ap_omti_phase_t fdc_phase;
  uint8_t fdc_command[AP_OMTI_FDC_COMMAND_MAX];
  unsigned fdc_command_length;
  unsigned fdc_command_index;
  uint8_t fdc_result[AP_OMTI_FDC_RESULT_MAX];
  unsigned fdc_result_length;
  unsigned fdc_result_index;

  /* One sector in flight, and where the host is within it. */
  uint8_t fdc_buffer[AP_AFD_SECTOR_BYTES];
  unsigned fdc_buffer_index;
  unsigned fdc_buffer_length;

  /* The head position per drive, which SENSE INTERRUPT STATUS reports as PCN
   * and SEEK and RECALIBRATE move. Two drives, per the Digital Output
   * register's A/B select. */
  uint8_t fdc_cylinder[2];
  /* Set by SEEK and RECALIBRATE, read and cleared by SENSE INTERRUPT STATUS --
   * which is the only way a driver learns a seek finished. */
  bool fdc_seek_done;
  uint8_t fdc_seek_st0;

  /* The floppy drive, caller-owned and optional, as the Winchester is. */
  ap_afd_t *floppy;

  /* The command phase. */
  ap_omti_phase_t phase;
  /* The last descriptor block's opcode and how many have run. State, not
   * instrumentation: a caller uses them to know a command completed. */
  uint8_t last_command;
  unsigned command_count;
  uint8_t command[AP_OMTI_CDB_LONG];
  unsigned command_length;
  unsigned command_index;

  /* The sector buffer, and how far through it the host is. */
  uint8_t buffer[AP_OMTI_BUFFER_BYTES];
  unsigned buffer_index;
  /* How many bytes the data phase in progress carries.
   *
   * This was derived from the command byte at every read -- "sense is four,
   * everything else is a whole sector" -- which is a special case per command
   * masquerading as a rule, and READ CONFIGURATION's ten bytes made it a third.
   * Whoever starts the phase knows the length; the read path should not have to
   * re-derive it. */
  unsigned transfer_length;
  /* Sectors still to move after the one in the buffer. **Wider than the CDB's
   * field**, because §5.1.2's count of zero means 256 and a byte cannot hold
   * it: storing it back in a byte turns the largest transfer the command can
   * ask for into no transfer at all. */
  unsigned blocks_left;
  uint32_t next_lba;

  /* §5.1.4's completion byte, and the sense the driver reads after a failure. */
  uint8_t completion;
  uint8_t sense[4];

  /* The drive, caller-owned and optional: a controller with no disk is a real
   * configuration and must not look like one with a blank disk. */
  ap_awd_t *drive;
} ap_omti_t;

/* Power-on: both halves. */
void ap_omti_reset(ap_omti_t *omti);

/* The fixed disk's own reset, reached by writing its status port. It must not
 * touch the floppy half -- `[OMTI]` §4.1 has them independent and §3.4 has them
 * running concurrently, so a disk reset that stopped the drive motors would be
 * a fault with no register to explain it. The floppy has its own reset in
 * Digital Output bit 2. */
void ap_omti_disk_reset(ap_omti_t *omti);

/* The fixed-disk half. */
[[nodiscard]] uint8_t ap_omti_disk_read(ap_omti_t *omti, unsigned reg);
void ap_omti_disk_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* The floppy half. `reg` is the offset within the eight-address block. */
[[nodiscard]] uint8_t ap_omti_fdc_read(ap_omti_t *omti, unsigned reg);
void ap_omti_fdc_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* Whether the floppy side is held in reset -- Digital Output bit 2 clear. */
[[nodiscard]] bool ap_omti_fdc_in_reset(const ap_omti_t *omti);

/* Whether the data register is byte-wide this moment, per the status C/D bit. */
[[nodiscard]] bool ap_omti_data_is_byte(const ap_omti_t *omti);

/* Attach a drive to the fixed-disk half, or `NULL` for none. Caller-owned. */
void ap_omti_attach(ap_omti_t *omti, ap_awd_t *drive);

/* Attach a diskette, or `NULL` for an empty drive -- which is distinct from a
 * blank one and reports itself as such: a command needing media answers with
 * ST1's No Data rather than a sector of zeroes. */
void ap_omti_attach_floppy(ap_omti_t *omti, ap_afd_t *floppy);

/* Which phase the floppy half is in, for the same reason the fixed disk exposes
 * its own: a test should assert the sequence, not re-derive it from the status
 * bits that are supposed to report it. */
[[nodiscard]] ap_omti_phase_t ap_omti_fdc_phase(const ap_omti_t *omti);

/* How many command bytes §6.3 gives an opcode, counting the opcode itself.
 * Zero for a code the section does not list, which is the INVALID path. */
[[nodiscard]] unsigned ap_omti_fdc_command_bytes(uint8_t opcode);

/* How many result bytes it produces. Zero is a real answer -- SPECIFY, SEEK and
 * RECALIBRATE have no result phase at all, and a driver that waits for one
 * hangs. */
[[nodiscard]] unsigned ap_omti_fdc_result_bytes(uint8_t opcode);

/* Which phase the fixed-disk half is in. Exposed for a test to assert the
 * sequence rather than infer it from the status bits, which is the thing the
 * status bits are supposed to report. */
[[nodiscard]] ap_omti_phase_t ap_omti_disk_phase(const ap_omti_t *omti);

/* The opcode of the descriptor block most recently completed, and a counter
 * that moves when one is. A caller watching the counter learns *that* a command
 * ran without having to decode the register traffic itself. */
[[nodiscard]] uint8_t ap_omti_last_command(const ap_omti_t *omti);
[[nodiscard]] unsigned ap_omti_command_count(const ap_omti_t *omti);

#endif /* APOLLO_DEVICE_AP_OMTI_H */
