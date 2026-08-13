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
#include "time/ap_time.h"

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
/* **Never set, and that is the model being honest.** These report a seek *in
 * progress*, and this core's `SEEK` and `RECALIBRATE` complete within the
 * command that issues them -- there is no interval during which one is
 * outstanding, so there is no moment at which the bit could be observed set. A
 * driver polling for "seek finished" reads the finished state, which is the
 * right answer arrived at by there being nothing to wait for.
 * They become real when seeks take time, which is the same boundary the fixed
 * disk's positioning has. */

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
  /* The drive is positioning and transferring. Added last so the values above
   * keep their numbers, which are hashed. */
  AP_OMTI_PHASE_EXECUTING,
} ap_omti_phase_t;

/* ## The drive's own figures, which is where an access time has to come from
 *
 * The controller does not determine access time -- the drive does -- and none
 * of `[OMTI]`'s three manuals gives one, which is correct of them. The DN3500
 * shipped 85, 170, 348 and 760 Mbyte drives from Micropolis and Maxtor, and the
 * image this core is developed against is the 348 Mbyte class.
 *
 * **`PROVISIONAL`: the drive identification, not the arithmetic.** That the
 * 348 Mbyte drive is specifically a Maxtor XT-4380E is inferred from capacity
 * and era rather than from Apollo documentation, and the geometry does not
 * match exactly -- this core's address conversion is 16 heads x 18 sectors
 * where the drive is 15 x 36 of half the sector size, the same bytes per track
 * in a different shape. The figures below are that drive's published ones and
 * each is applied as its own component, so replacing the drive replaces
 * numbers rather than structure. See `docs/PROJECT_STATUS.md`.
 *
 * Deliberately *not* MAME's figure: `omti8621.cpp` uses a flat 1 ms while its
 * own comment puts the average at ~30 ms, so matching it would be matching a
 * known approximation. */
#define AP_OMTI_DRIVE_RPM 3600u
/* Half a revolution is the average rotational wait for a sector to arrive. */
#define AP_OMTI_ROTATION_TIME (AP_TIME_BASE_HZ * 60u / AP_OMTI_DRIVE_RPM)
#define AP_OMTI_AVERAGE_LATENCY (AP_OMTI_ROTATION_TIME / 2u)
/* "Average seek 16 ms typical", against 3.0 ms track-to-track and 29 ms full
 * stroke. Distance-dependent seek is a tail: this core does not model where the
 * heads are, so every seek costs the average. */
#define AP_OMTI_AVERAGE_SEEK (AP_TIME_BASE_HZ / 1000u * 16u)
/* 1.25 Mbyte/s sustained. */
#define AP_OMTI_TRANSFER_BYTES_PER_SEC 1250000u

/* ## The sector buffer, and the one command that reads it whole
 *
 * A data command moves sectors **one at a time** through this buffer, whatever
 * block count it names -- §5.1.2's block count is a byte, so a READ can ask for
 * 255 sectors, and the controller's own buffer is refilled per sector.
 *
 * `0E READ DATA FROM SECTOR BUFFER` is the exception, and it is why the buffer
 * is not one sector: §5.4.13 returns "the jumper selectable sector size times
 * the block count specified in byte 4 ... up to a maximum block count as
 * follows" --
 *
 *     Sector Size   Block or Sector Count
 *     512           15
 *     1024           7
 *     1056           7
 *
 * **That table is one buffer size's instance of a rule, not the rule.** It was
 * transcribed as an absolute cap and it is not: §5.4.19 prints the same three
 * rows and introduces them with "The number of data blocks that can be read is
 * **limited by the controller's Buffer size** as follows", and byte `14` of the
 * identification block below enumerates four buffer sizes -- 2K, 8K, 16K and
 * 32K. A single fixed table cannot describe four parts. The rows themselves say
 * which one they are: 15x512, 7x1024 and 7x1056 all fall just under **8192**.
 *
 * This machine's controller reports **32K**, so the cap it was refusing at was
 * another part's. The machine told the host it had 32K and then refused eight
 * 1056-byte blocks, which is its own identification contradicting its own
 * behaviour -- and Domain/OS asks for exactly eight, so a real controller that
 * refused would have broken the real machine. It arrived here as `invalid disk
 * address`, which is `refuse()`'s sense code, in a run whose report showed no
 * refusals because this arm never reaches the refusal counter.
 *
 * So the cap is derived from the buffer the part reports.
 *
 * `PROVISIONAL` in its exact boundary, and marked as such rather than rounded:
 * the manual gives the rule and one table, not the table for 32K. Two of that
 * table's rows are `floor(8192/size) - 1` and the third is `floor(8192/1056)`,
 * so whether a 32K part stops at 31 or at 30 is not settled by anything on the
 * page. The figure below is the plain division; the ambiguity is one block wide
 * and no observed command comes near it. */
#define AP_OMTI_BUFFER_RAM_BYTES 32768u
#define AP_OMTI_MAX_BUFFER_BLOCKS                                              \
  (AP_OMTI_BUFFER_RAM_BYTES / AP_AWD_SECTOR_BYTES)

/* §5.4.27 and §5.4.28: READ LONG and WRITE LONG move "the jumper selected
 * sector size (512, 1024 or 1056) of data plus 4 bytes (for ST506/412 drives)
 * or **6 bytes (for ESDI drives)** of ECC data". This machine's drives are
 * ESDI, so a long block is six bytes wider than a sector, and the staging
 * buffer is sized for the widest block the command set can ask for rather than
 * for a sector. */
#define AP_OMTI_ECC_BYTES 6u
#define AP_OMTI_LONG_BLOCK_BYTES (AP_AWD_SECTOR_BYTES + AP_OMTI_ECC_BYTES)
#define AP_OMTI_BUFFER_BYTES                                                   \
  (AP_OMTI_MAX_BUFFER_BLOCKS * AP_OMTI_LONG_BLOCK_BYTES)

/* §5.4.22's reply is a fixed 256 bytes: a six-byte dated header, then five-byte
 * defect descriptors, then five `FFh` for the end of the list. */
#define AP_OMTI_DEFECT_LIST_BYTES 256u

/* §5.4.16's "ALTERNATE TRACK ADDRESS Descriptor Block", four bytes sent during
 * the data-out phase. Its head and eleven-bit cylinder sit in exactly the field
 * positions a descriptor block's bytes 1-3 use, which is why the same decoder
 * reads it. */
#define AP_OMTI_ALTERNATE_ADDRESS_BYTES 4u

/* §5.4.3, sense byte 0's top bit: "Bit 7 set to 1 indicates the validity of the
 * sector address. If bit 7 is set to 0, the sector address is not valid." So it
 * is a property of bytes 1-3 rather than part of the code, and a controller
 * that knows where a command failed and leaves it clear is withholding an
 * answer it has. */
#define AP_OMTI_SENSE_ADDRESS_VALID 0x80u

/* ## The identification block, which is what a reset leaves in the buffer
 *
 * §5.4.13 again: "The READ BUFFER Command can also be used to model and status
 * information about the controller. If a READ BUFFER Command is issued after a
 * RESET is done (before any other command) the first XX bytes in the buffer
 * contain the following information."
 *
 *     00 through 0D   8x2xVW.WMMDDYY
 *     0E - 0F         ROM checksum Word
 *     10 bit 0        ROM checksum error
 *     11 bit 0        Processor Register error
 *     12 bit 0        Buffer Ram error
 *     13 bit 0        Sequencer Register File Error
 *     14 bits 7 & 6   0-0 2K, 0-1 8K, 1-0 16K, 1-1 32K buffer size
 *     20-2F, 30-3F, 50-5F   LUN 0, 1 and 3 default values
 *
 * So the controller identifies itself and reports its own power-on diagnostics
 * through a data command, and "after a RESET before any other command" needs no
 * flag to model: a reset *writes* the block into the buffer and whatever command
 * comes next overwrites it, which is the same sentence from the other side.
 *
 * The template resolves against the part the DN3500 has. `8x2x` is the model,
 * `8621`; `VW.W` is the literal `V` and a version; `MMDDYY` a date. The oracle
 * carries the actual string for the Apollo-shipped controller --
 * `8621VB.4060487` -- version B.4, dated 4 June 1987, which is a value read off
 * the part rather than anything this project could derive. It writes `xx` into
 * the two checksum bytes, which is plainly a placeholder and not a checksum, so
 * those are left **zero** here and named as unknown rather than copied.
 *
 * The four error bytes are zero on a healthy controller, and the boot PROM's
 * Winchester test 1 checks exactly that: it reads eight words past them and
 * then requires the two words at `10`-`13` to be zero. */
#define AP_OMTI_IDENTIFICATION "8621VB.4060487"
#define AP_OMTI_IDENTIFICATION_BYTES 14u
#define AP_OMTI_ID_ROM_CHECKSUM 0x0Eu
#define AP_OMTI_ID_ERROR_FLAGS 0x10u
#define AP_OMTI_ID_BUFFER_SIZE 0x14u
/* Bits 7 and 6 set: 32K, per the table above. */
#define AP_OMTI_ID_BUFFER_32K 0xC0u

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

  /* The address a data command was refused for, and how many were.
   *
   * Deliberately outside the state hash, like the machine's bus-error count and
   * for the same reason: this is our record of *watching* the controller, not
   * state the controller has. A sense byte says "illegal disk address" and does
   * not say which address, and a driver that asks for one sector the geometry
   * does not have looks exactly like a driver that asks for a thousand. */
  uint16_t refused_cylinder;
  uint8_t refused_head;
  uint8_t refused_sector;
  uint32_t refused_lba;
  unsigned refusals;
  /* The six bytes as the driver wrote them. Decoded fields say what *we* made
   * of the command; these say what it actually sent, and the difference between
   * those two is exactly where a decode error hides. */
  uint8_t refused_cdb[6];

  /* The last few sector addresses read, newest last. A refusal names the block
   * the driver *asked* for; this names the blocks it read to work that address
   * out, which is the question when the address is wild. Sixteen because the
   * pointer that sent a driver somewhere impossible came out of a sector it had
   * just read.
   *
   * Sixteen was too short a window. The sixteen sectors before the fault turned
   * out to be one contiguous extent -- block indices 911 to 926 mapping
   * linearly onto disk addresses -- so the address the driver went wrong on
   * came from further back, and the whole sequence is what shows where a walk
   * stopped being sequential. A boot reads about fifteen hundred, so this holds
   * all of them. */
  uint32_t recent_reads[2048];
  unsigned recent_read_count;

  /* ## The commands themselves, which the sector list cannot substitute for
   *
   * A sector list says what came off the surface. It does not say *which
   * command* took it, how many blocks that command asked for, or how many the
   * host then drained -- and the pairing `1E READ DATA TO BUFFER` / `0E READ
   * DATA FROM SECTOR BUFFER` is exactly where those three come apart: `1E`
   * fills the buffer and `0E` empties it, in separate commands, so a buffer
   * refilled before it is drained loses a sector without any command failing.
   *
   * Recorded for every command that names an address or moves data, newest
   * last. `blocks` is what the CDB asked for; `lba` is the first sector, or
   * `UINT32_MAX` for the commands that name none. */
  struct {
    uint8_t command;
    uint16_t blocks;
    uint32_t lba;
    /* Bytes the host actually took out of the data port while this command was
     * the current one. A command that offers 2,112 bytes and has 1,056 read is
     * a *successful* command that delivered half a transfer, and nothing else
     * in the model records that. */
    uint32_t drained;
  } recent_commands[512];
  unsigned recent_command_count;
  /* Set by SEEK and RECALIBRATE, read and cleared by SENSE INTERRUPT STATUS --
   * which is the only way a driver learns a seek finished. */
  bool fdc_seek_done;
  uint8_t fdc_seek_st0;

  /* The floppy drive, caller-owned and optional, as the Winchester is. */
  ap_afd_t *floppy;

  /* The command phase. */
  ap_omti_phase_t phase;

  /* ## A command takes time, and Domain/OS requires that it does
   *
   * The completion this controller is working towards, and the instant it
   * arrives. While `phase` is `AP_OMTI_PHASE_EXECUTING` the drive is busy and
   * these hold what `finish` will apply when the deadline passes.
   *
   * **This exists because a zero access time crashes Domain/OS.** Measured
   * 2026-08-12: with completion instantaneous, `IRQ14` from one of a boot's
   * `READ DATA TO BUFFER` commands lands *inside* the kernel's page-fault
   * handler, which is interrupted part-way through a copy; what follows finds
   * resource locks held and calls `crash_system`, printing `CRASH_STATUS
   * 00120020` -- `002398-04`'s *supervisor fault while resource lock(s) set*.
   * MAME carries a deliberate fix for the same failure and says so in a
   * comment: "Domain/OS doesn't expect zero access time". Detail in
   * `docs/PROJECT_STATUS.md`.
   *
   * ### The approximation this still carries, named rather than glossed
   *
   * **The data phase is not delayed, only the completion is.** A `READ` fills
   * the buffer and offers its bytes to the host at once, and the access time is
   * spent afterwards, before the completion byte is announced. On the drive the
   * order is the other way round: nothing can be read until the heads have
   * arrived and the sector has come round, so `REQ` should not go up until
   * then.
   *
   * Cost to close: a driver that times its *transfer* rather than its command
   * sees the bytes arrive early. Nothing measured so far depends on it --
   * Domain/OS waits on the completion -- and the fix is to move the deadline
   * ahead of the phase change rather than after it, which is a change to when
   * `enter_data_phase` is reached rather than to any figure here. MAME makes
   * the same approximation and marks it `FIXME: should delay m_omti_state and
   * m_status_port as well`. */
  ap_time_t completion_at;
  /* The clock this device is advanced against, so a deadline can be set from
   * inside a register write that has no `now` of its own. */
  ap_time_t now;
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

  /* Where a WRITE LONG in progress will place its blocks once the host has
   * finished handing them over.
   *
   * `blocks_left` cannot carry this: §5.4.28's blocks are 1062 bytes wide, not
   * 1056, so the data-out path's sector-at-a-time drain does not fit them, and
   * the whole transfer is staged and placed at the end. Zero blocks means no
   * long write is in progress, which is what separates this from `0F`, whose
   * data-out phase ends by placing nothing at all. */
  uint32_t long_write_lba;
  unsigned long_write_blocks;

  /* An ASSIGN ALTERNATE TRACK waiting for §5.4.16's four-byte descriptor. Like
   * `long_write_blocks`, it says what the data-out phase now in progress is
   * *for*: three commands end that phase in three different ways, and the phase
   * itself cannot tell them apart. */
  bool assigning_alternate;

  /* §5.1.4's completion byte, and the sense the driver reads after a failure. */
  uint8_t completion;
  uint8_t sense[4];

  /* The drive, caller-owned and optional: a controller with no disk is a real
   * configuration and must not look like one with a blank disk. */
  ap_awd_t *drive;
  /* The drive the *current command* addresses, which is not always the drive
   * that is attached. §5.1.1 gives byte 1 bit 5 of every command block as the
   * Logical Unit Number, and a controller answers for the unit named there or
   * reports that it is not ready. This model attaches one drive, LUN 0, so a
   * command for LUN 1 selects nothing -- and must say so rather than serve
   * drive 0's image under drive 1's name. Set at the top of each command and
   * held through its data phase. */
  ap_awd_t *selected;
  /* The LUN that command named, kept for the completion byte: §5.3's status
   * register puts it in bit 5, "the LUN address of the device associated with
   * this command". */
  uint8_t command_lun;
} ap_omti_t;

/* Power-on: both halves. */
void ap_omti_reset(ap_omti_t *omti);

/* Advance the controller to `now`, retiring a command whose access time has
 * elapsed. Called every tick from the board, like every other timed device: the
 * completion it retires raises `IRQ14`, and *when* it does is the difference
 * between Domain/OS booting and crashing. */
void ap_omti_advance(ap_omti_t *omti, ap_time_t now);

/* The fixed disk's own reset, reached by writing its status port. It must not
 * touch the floppy half -- `[OMTI]` §4.1 has them independent and §3.4 has them
 * running concurrently, so a disk reset that stopped the drive motors would be
 * a fault with no register to explain it. The floppy has its own reset in
 * Digital Output bit 2. */
void ap_omti_disk_reset(ap_omti_t *omti);

/* The fixed-disk half. */
[[nodiscard]] uint8_t ap_omti_disk_read(ap_omti_t *omti, unsigned reg);

/* ## The data port is sixteen bits wide, and the board has to say so
 *
 * §4.2 makes the data port "byte-wide when C/D is set, word-wide when it is
 * clear" -- byte at a time while commands and status are moving, a word at a
 * time while *data* is. This module has said that since it was written; what it
 * had no way to express was a word **cycle**, because the board decomposed
 * every access into bytes and a byte read of the data port is a different
 * event from half of a word read of it.
 *
 * The boot PROM's Winchester test 1 is the first thing to need it: after
 * `0E READ DATA FROM SECTOR BUFFER` it reads the identification block with
 * `MOVE.W $4D000`, eight times, and then requires two more words to be zero.
 * Served as two byte reads, the second byte is the *status* register and the
 * word can never be zero -- it came back `FFFF`.
 *
 * ## Which buffer byte is the word's high half: settled, and not the oracle's
 *
 * This was `PROVISIONAL` for exactly one commit. The oracle packs the first
 * byte into bits 7-0 and the second into 15-8, which delivers a buffer to a
 * big-endian CPU byte-swapped; that was followed rather than second-guessed,
 * because nothing then in hand distinguished the two -- the identification
 * bytes the firmware compares are all zero, and no boot PROM contains the
 * string `8621`.
 *
 * The thing that settles it is what the note asked for: **a transfer of known
 * content through 16-bit programmed I/O**. The boot PROM loads `sysboot` from
 * the disk to `010FD800` and requires the first long word to be `0013D800`. It
 * arrived as
 *
 *     010FD800  13 00 00 D8 ...
 *     010FD810  59 53 42 53 4F 4F 20 54 45 52 20 56   YSBSOO TER V
 *
 * -- `0013D800` and `SYSBOOT VER ` with the bytes of every word exchanged. Two
 * independent confirmations in one read: a magic number the firmware names, and
 * a string that is only a string one way round.
 *
 * So the **earlier buffer byte belongs in the high half**, which is also what
 * makes a sector moved by word accesses land in memory in disk order. The
 * oracle disagrees and this is one of the places `CLAUDE.md` expects it to:
 * `omti8621.cpp`'s `get_data` builds `buffer[i] | buffer[i+1] << 8`. */
[[nodiscard]] uint16_t ap_omti_disk_read16(ap_omti_t *omti);
void ap_omti_disk_write16(ap_omti_t *omti, uint16_t value);
void ap_omti_disk_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* The floppy half. `reg` is the offset within the eight-address block. */
[[nodiscard]] uint8_t ap_omti_fdc_read(ap_omti_t *omti, unsigned reg);
void ap_omti_fdc_write(ap_omti_t *omti, unsigned reg, uint8_t value);

/* Whether the floppy side is held in reset -- Digital Output bit 2 clear. */
[[nodiscard]] bool ap_omti_fdc_in_reset(const ap_omti_t *omti);

/* Whether the data register is byte-wide this moment, per the status C/D bit. */
[[nodiscard]] bool ap_omti_data_is_byte(const ap_omti_t *omti);

/* Whether the fixed-disk side is asking for `IRQ14`.
 *
 * A level, derived, not a latch: §4.2 gives the raise -- "If the INTERRUPT
 * ENABLE bit was previously set in the MASK register, the REQ bit is set in the
 * STATUS byte, along with IRQ14 on the system bus" -- and §4.3 gives the clear,
 * "the controller clears the IREQ and IRQ14 (if enabled)" when the status byte
 * is read. Both sides are already visible in state this part keeps, so the
 * condition is `IREQ` and the enable bit together and there is nothing left to
 * invent.
 *
 * This is what the board's own comment was waiting for. The boot PROM's driver
 * polls, so the machine loaded an operating system without it; Domain/OS's
 * driver waits for the interrupt, and printed `DISK TIMEOUT` when it never
 * came. */
[[nodiscard]] bool ap_omti_disk_irq(const ap_omti_t *omti);

/* Whether the fixed-disk side is asking for a DMA cycle -- `DRQ7` on this
 * board, Table 2-4's 16-bit Winchester line.
 *
 * The same derivation as the interrupt, from the same register: §4.3 gates
 * `DREQ` on the MASK byte's DMA ENABLE -- "If the DMA ENABLE bit in the MASK
 * byte has been previously set, data will be transferred in DMA mode ... it
 * will set the DREQ bit" -- and the controller already raises and lowers it
 * around a data phase. So the line is the bit, and asking for it is not
 * inventing a condition.
 *
 * `board/ap_disk.h` said this had no line because "nothing in this controller
 * knows a transfer is in progress" while only the register sets were modelled,
 * and that "It gains a line when the command sets do." They do now. */
[[nodiscard]] bool ap_omti_disk_dma_request(const ap_omti_t *omti);

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

/* The three modifiers in the top bits of a floppy command's first byte -- §6.3
 * gives `MT` multitrack, `MF` MFM-rather-than-FM and `SK` skip-deleted-data.
 *
 * They were **defined and never read**, so every floppy command ran as if all
 * three were clear. These report what the command in progress asked for.
 *
 * What each *does* here is bounded by the image format, and honestly so. `MT`
 * is real: §6.3 has a multitrack read continue onto the second head of the same
 * cylinder rather than ending at the last sector. `MF` selects the recording
 * encoding, and an `.afd` stores sector data with no encoding to select. `SK`
 * skips sectors marked with a deleted-data address mark, and an `.afd` has no
 * address marks to mark them with. So `MT` is actionable and the other two are
 * format-limited in the same way the `.awd` ID field was before its sidecar --
 * reported, so a driver can see its request was understood, and not silently
 * dropped. */
/* Whether a drive's motor is running -- Table 4-3's `DOR` bits 4 and 5, which
 * were stored and never read. */
[[nodiscard]] bool ap_omti_fdc_motor_on(const ap_omti_t *omti, unsigned drive);

[[nodiscard]] bool ap_omti_fdc_multitrack(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_fdc_mfm(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_fdc_skip_deleted(const ap_omti_t *omti);

/* ## The floppy's own two lines: `IRQ6` and `DRQ2`
 *
 * `008778-03` Table 2-3 gives `IRQ6` as the floppy disk's, and Table 2-4 gives
 * `DRQ2`. Both were placed on the board and left undriven, with a comment
 * saying they land with the floppy's own item -- because the fixed disk's
 * `IREQ` bit had no counterpart here to derive them from.
 *
 * It does have one. Table 4-3's Digital Output Register bit 3, `AP_OMTI_DOR_INT_DMA`,
 * gates both: the AT floppy controller raises `IRQ6` and `DRQ2` only when the
 * host has enabled them there, exactly as the fixed disk's `IREQ` is gated on
 * its MASK register. So the same shape, from the register this half actually
 * has.
 *
 * `IRQ6` follows the **result phase**, which is the FDC's completion: §6.4's
 * result bytes are waiting and `MSR` says so. A driver that waits rather than
 * polls needs this edge for the same reason Domain/OS needed `IRQ14`.
 *
 * `DRQ2` follows the **execution phase**, where a byte is ready to move. That
 * is the condition the board's comment named as "a different condition from"
 * the fixed disk's, and it is: this one is about a byte in flight, not a
 * command completing. */
[[nodiscard]] bool ap_omti_fdc_irq(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_fdc_dma_request(const ap_omti_t *omti);

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

/* How many addresses the controller refused, and the last of them. `cylinder`,
 * `head` and `sector` are what the command named; `lba` is the linear sector a
 * multi-block read had reached, and only one of the two is meaningful for any
 * given refusal -- which is why both are reported rather than one derived. */
[[nodiscard]] unsigned ap_omti_refusals(const ap_omti_t *omti);
[[nodiscard]] uint16_t ap_omti_refused_cylinder(const ap_omti_t *omti);
[[nodiscard]] uint8_t ap_omti_refused_head(const ap_omti_t *omti);
[[nodiscard]] uint8_t ap_omti_refused_sector(const ap_omti_t *omti);
[[nodiscard]] uint32_t ap_omti_refused_lba(const ap_omti_t *omti);
/* The six command bytes of the last refused command, as they arrived. */
[[nodiscard]] const uint8_t *ap_omti_refused_cdb(const ap_omti_t *omti);

/* How many sectors have been read, and the last of them. `index` counts back
 * from the newest: 0 is the most recent. Answers false past what is kept. */
[[nodiscard]] unsigned ap_omti_reads(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_recent_read(const ap_omti_t *omti, unsigned index,
                                       uint32_t *lba);

/* The same, for commands: how many have been recorded, and the `index`th
 * counting back from the newest. `lba` answers `UINT32_MAX` for a command that
 * addresses nothing. */
[[nodiscard]] unsigned ap_omti_commands_recorded(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_recent_command(const ap_omti_t *omti, unsigned index,
                                          uint8_t *command, uint16_t *blocks,
                                          uint32_t *lba, uint32_t *drained);

#endif /* APOLLO_DEVICE_AP_OMTI_H */
