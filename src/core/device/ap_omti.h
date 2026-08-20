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
  /* Read CONFIGURATION, write SELECT (a function). §4.2: the configuration byte
   * is "the status of the drive configuration jumpers", bits 7-4 not used and
   * set to 1, bits 3-0 the straps `W20`-`W23` on an 8620/8627. `002398-04`
   * p. 12-9 calls the same register the **Jumper Setting Register** and names
   * its four bits `j1`-`j4`, "1 => jumperN in place" -- so the measured `FC`
   * this core answers with is jumpers 1 and 2 fitted and 3 and 4 out. */
  AP_OMTI_DISK_CONFIG = 2u,
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
/* §4.2's MASK register: "Enables and disables interrupts and DMA transfers.
 * Bits 7-2 Not used. **Bit 1 INTERRUPT ENABLE** ... **Bit 0 DMA ENABLE** ...
 * 1 = DREQ is gated onto system bus".
 *
 * `002398-04` p. 12-9 draws the same register with the two labels the other way
 * round -- `dma` at bit 1, `int` at bit 0. The part's own manual is followed,
 * and the oracle is on its side: `omti8621.cpp` has `OMTI_MASK_DMAE 0x01` and
 * `OMTI_MASK_INTE 0x02`. Recorded because the handbook is otherwise a good
 * second source for this board and a reader comparing the two pages needs to
 * know which was preferred and why -- the same page also transposes the status
 * register's address and draws the floppy's reset polarity backwards. */
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
 * 6 are Reserved."
 *
 * `002398-04` p. 12-14 names all seven of those reserved bits -- `WG` write
 * gate, `HD3` "head select 3 / reduced write current", `HD2`, `HD1`, `HD0`,
 * `UN1` and `UN0` -- which is the drive-interface state read back rather than a
 * register the host writes. Not modelled, and the reason is the same one that
 * keeps the ECC bytes zero: an `.afd` image has no write gate and no reduced
 * write current, so three of the seven have no value this core could give that
 * would not be invented. Recorded because `[OMTI]` calls them reserved and this
 * is the only page that says what they are. */
#define AP_OMTI_DIR_DISK_CHANGE 0x80u

/* The Additional Control Register at AT `3F6`, which `[OMTI]` Table 4-3 lists by
 * name and does not decompose. `002398-04` p. 12-14 is the only document on this
 * shelf that gives its fields:
 *
 *     7-6  reserve
 *     5    PN6   interface pin 6
 *     4    PN4   interface pin 4
 *     3    PN2   interface pin 2 (density & speed control)
 *     2    WP2 |
 *     1    WP1 |  write precompensation, three bits
 *     0    WP0 |
 *
 * The register is accepted and kept, and none of the three functions is
 * modelled -- honestly, not by omission. Write precompensation shifts *when* a
 * bit cell is written to counter peak shift on the inner tracks; density and
 * speed control select the medium's recording rate at the drive. Both are
 * properties of a magnetic surface, and an `.afd` image is decoded sector data
 * with no surface underneath it, so there is no quantity here for either to
 * change. `PN4` and `PN6` are interface pins this document names and does not
 * define.
 *
 * They are named so that a driver's writes can be *read* -- `ap_omti_fdc_...`
 * exposes the byte -- rather than disappearing into a field nobody can decode.
 */
#define AP_OMTI_FDC_CONTROL_PRECOMP_MASK 0x07u
#define AP_OMTI_FDC_CONTROL_PIN2 0x08u /* density and speed control */
#define AP_OMTI_FDC_CONTROL_PIN4 0x10u
#define AP_OMTI_FDC_CONTROL_PIN6 0x20u

/* The Diskette Control Register at AT `3F7` written -- the data rate, from the
 * 8640 manual's §5.1. See `fdc_rate` in the structure below for why these are
 * kept and not acted on, and for what it cost to have them share `3F6`'s byte. */
#define AP_OMTI_FDC_RATE_MASK 0x03u
typedef enum {
  AP_OMTI_FDC_RATE_500K = 0u,
  AP_OMTI_FDC_RATE_300K = 1u,
  AP_OMTI_FDC_RATE_250K = 2u,
  AP_OMTI_FDC_RATE_RESERVED = 3u,
} ap_omti_fdc_rate_t;

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
/* **Now real.** These used to be documented here as "never set, and that is the
 * model being honest": `SEEK` and `RECALIBRATE` completed inside the command
 * that issued them, so there was no interval during which one was outstanding
 * and no moment at which the bit could be observed set. That note ended "they
 * become real when seeks take time", and `008778-03` chapter 7 is the document
 * that supplies the times -- see `AP_OMTI_FDC_TRACK_TO_TRACK` below.
 *
 * The polled path they serve is the whole mechanism, and it is the documented
 * one: `[OMTI]` §4.5 describes the floppy protocol as command phase, busy,
 * results, and **names no interrupt anywhere in it**. Neither does §6.3 for
 * `SEEK` or `RECALIBRATE`, which have no result phase to raise one from. So a
 * driver learns a seek has finished by watching its drive's bit here fall and
 * then issuing `SENSE INTERRUPT STATUS` -- which is exactly what a per-drive
 * "in the Seek mode" bit is for, and why the part has two of them. No
 * seek-completion interrupt is modelled, because no manual on this shelf
 * describes one. */

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
 * what a driver checks first.
 *
 * §6.4.1's other two entries are constants: "**Bit 3 and 2  Not Used - Always
 * zero**", then bits 1-0 the unit. `002398-04` p. 12-13 gives those two bits
 * names -- `NR` not ready at 3, `HD` head address at 2 -- which is the generic
 * 765's register, as its ST3 is. The part's own manual is followed here for the
 * reason set out at `AP_OMTI_ST3_*`, and both are recorded so that a reader who
 * finds the handbook's page knows this file has seen it. */
#define AP_OMTI_ST0_IC_MASK 0xC0u
#define AP_OMTI_ST0_IC_NORMAL 0x00u  /* completed and properly executed */
#define AP_OMTI_ST0_IC_ABRUPT 0x40u  /* started, not successfully completed */
#define AP_OMTI_ST0_IC_INVALID 0x80u /* never started */
#define AP_OMTI_ST0_IC_NOT_READY 0xC0u /* 'ready' changed state mid-command */
#define AP_OMTI_ST0_SEEK_END 0x20u
#define AP_OMTI_ST0_EQUIPMENT 0x10u /* fault, or no track 0 after 77 steps */
/* ## `ST0[3]` and `ST0[2]`, which this file named nowhere
 *
 * `002398-04` p. 8-13 lays the register out bit by bit and gives both:
 * "**D3 - Set if FDD Not Ready**" and "**D2 - Head Address**". Neither had a
 * constant here and neither was ever set, so `ST0` came back with the interrupt
 * code, the seek-end and equipment bits, and the unit -- and nothing else.
 *
 * The head bit is the one that misleads: it is "the state of the head at the end
 * of the execution phase", so a read from side 1 returned an `ST0` saying side
 * 0. The result bytes carry `H` separately and correctly, which is why nothing
 * noticed; a driver that checks `ST0` rather than the `H` byte would have been
 * told the wrong side of every diskette.
 *
 * `NR` is the empty drive's. This model reported that case in `ST1` alone --
 * no-data plus missing-address-mark, which is what a *seek* into nothing looks
 * like rather than what an absent drive looks like. The interrupt code stays
 * `01` and does not become `11`: p. 8-13 reserves `11` for "FDD went not ready
 * **during** command execution", which is a drive that was ready when the
 * command started. */
#define AP_OMTI_ST0_NOT_READY 0x08u
#define AP_OMTI_ST0_HEAD 0x04u
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

/* ST3, and the one register on this part that two manuals describe differently.
 *
 * `[OMTI]` §6.4.4 -- the controller's own manual, and the 8640's §5.6.4 word for
 * word -- gives three live bits and five constants: bit 6 Write Protect, bit 4
 * Track 0, bit 2 Head Address, and bits 7, 5, 3 and 1 "not used - always zero"
 * with bit 0 "not used - always 1". That is what is modelled below.
 *
 * `002398-04` p. 12-14 draws the **generic 765 register** for the same machine
 * and names all eight: `FT` fault, `WP` write protect, `RDY` ready, `TR0` track
 * 0, `TS` two sided, `HD` head address, `UN1` and `UN0` unit select -- each
 * annotated "from drive". The three both documents agree on sit in the same
 * three positions, so the disagreement is confined to the five OMTI calls
 * constant.
 *
 * **Which to follow, and why the part's own manual wins here.** The handbook's
 * table is the µPD765's, and `008778-03` §5.4.1.2 does name the part as an
 * "FDC765" -- so the *silicon* has these bits. What OMTI's manual describes is
 * what they read as once the AT cabling is attached, and a drive that supplies
 * no fault, ready or two-side line leaves them at their tied values. Two OMTI
 * manuals say the same thing about the same board; against them stands one page
 * of a handbook whose adjacent pages transpose this controller's mask register
 * bits and print its floppy reset polarity backwards.
 *
 * **The same table appears a third time, and where it appears is the point.**
 * `002398-04` p. 8-14 prints all eight bits again -- `FT`, `WP`, `RDY`, `TR0`,
 * `TS`, `HD`, `UN1`, `UN0` -- in chapter 8, for the **DN400/420/600**. That
 * machine's address space has "Floppy Status", "Floppy I/O" and "Floppy DMA" at
 * `8C00` with **no OMTI board anywhere in it**: a bare 765 wired straight to the
 * drive, where all eight bits genuinely do come from the drive and the table is
 * simply right.
 *
 * So the handbook is not making an independent claim about the DN3000's board
 * twice. It is printing the **part's** register, correctly for the family that
 * has the part bare, and again in chapter 12 for a family that has it behind an
 * OMTI controller. That is exactly the shape `[OMTI]` §6.4.4 describes -- the
 * silicon has the bits, the board ties five of them -- and it makes the two
 * documents complementary rather than contradictory.
 *
 * **Still not decided by that**, because it is an explanation and not a
 * measurement. **What would settle it**: a driver that branches on bit 5. If the
 * DN3000's floppy driver polls ST3 for `RDY` before using the drive, the
 * handbook's reading is the machine's and this model would hang instead; no boot
 * measured here reaches the floppy, so nothing has yet distinguished them. Named
 * in `docs/PROJECT_STATUS.md` rather than decided by preference.
 *
 * Bit 4's description in *both* OMTI manuals contradicts its own name: "Track 0
 * (TO) - Status of the 'ready' signal from the diskette drive". The name is
 * modelled and the sentence is not, because bit 4 is Track 0 on every
 * 765-family part and a drive-ready bit that moves when the head reaches
 * cylinder 0 would be reported to a driver as readiness it never gained. The
 * handbook explains the sentence rather than the name: `RDY` is the bit above,
 * and its description slid one row down OMTI's table. */
#define AP_OMTI_ST3_WRITE_PROTECT 0x40u
#define AP_OMTI_ST3_TRACK_0 0x10u
#define AP_OMTI_ST3_HEAD 0x04u
#define AP_OMTI_ST3_ALWAYS 0x01u
/* The five `002398-04` p. 12-14 names and `[OMTI]` §6.4.4 calls constant.
 * Defined so that a reader who finds the handbook's table can see which bits it
 * means, and so that the day the question above is settled the positions are
 * already written down. Nothing sets them. */
#define AP_OMTI_ST3_FAULT 0x80u
#define AP_OMTI_ST3_READY 0x20u
#define AP_OMTI_ST3_TWO_SIDED 0x08u
#define AP_OMTI_ST3_UNIT_MASK 0x03u

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
 * of `[OMTI]`'s three manuals gives one, which is correct of them. The figures
 * are the *drive's*, and Apollo published them: `008778-03` §6.4 lists the
 * approved Winchester drives by manufacturer's part number and tabulates each
 * one's operating characteristics.
 *
 * **The `PROVISIONAL` this block used to carry is closed.** That the 348 Mbyte
 * drive is a Maxtor was inferred from capacity and era; Table 6-5 names it
 * outright -- **"Maxtor EXT-4380"** -- and Table 6-4 names the 155 Mbyte drive
 * as the **Micropolis 1355**, which is the pair `image/ap_awd.h` had taken from
 * the oracle's disk types. Apollo's own manual and MAME agree on which two
 * drives this machine shipped.
 *
 * Table 6-5's four timings for the 348 Mbyte drive, which is the class this
 * core's image is:
 *
 *     Transfer rate            10   (megabits/second, NRZ -- see below)
 *     Single cylinder seek      4 msec (typical)
 *     1/3 stroke seek          30 msec (typical)
 *     Maximum stroke seek      58 msec (typical)
 *     Average latency        8.33 msec
 *     Nominal rpm            3600
 *
 * **1/3 stroke *is* the average seek**, and this document says so by its own
 * layout: Table 6-3 gives the 72 Mbyte drive "Track-to-track / **Average** /
 * Maximum" in the three row positions where Tables 6-4 and 6-5 give "Single
 * cylinder / **1/3 stroke** / Maximum stroke". A uniformly random seek over N
 * cylinders travels N/3 on average, which is why the industry quotes the
 * third-stroke figure as the average one.
 *
 * So the average seek is **30 ms**, not the 16 ms this core carried. The 16
 * came from the published sheet of a *Maxtor XT-4380E* -- a part number that
 * was inferred, and a different one from the EXT-4380 Apollo approved.
 * `omti8621.cpp` uses a flat 1 ms and its own comment puts the average at
 * ~30 ms, so the oracle's *comment* and Apollo's *table* agree with each other
 * and the inferred datasheet was the outlier. (MAME's 1 ms is still not
 * followed: it is a known approximation its own author flagged.)
 *
 * Distance-dependent seek is still a tail here, and deliberately: the fixed
 * disk has no modelled head position, so every seek costs the average. The
 * floppy *does* -- see `AP_OMTI_FDC_AVERAGE_SEEK` below, where the same
 * document's figures compose into a distance model that reproduces its own
 * published average.
 *
 * ## A second Apollo document, 10% apart on the same drive
 *
 * `002398-04` p. 6-3 tabulates seek times for every approved drive, and its row
 * for `604 Max 380` -- the 348 Mbyte Maxtor this core's image is -- reads
 * `T-to-T 4, AVG 27, MAX 52, RPM 3600, AVG LATENCY 8.33, TRANSFER RATE 1.25,
 * AVG READ 35.3`.
 *
 * Track-to-track, rpm, latency and transfer rate all agree with Table 6-5 to the
 * digit. The two seek figures do not: **27 against 30, and 52 against 58**. That
 * is a uniform ~10% -- 27/30 is 0.900 and 52/58 is 0.897 -- with the
 * single-cylinder figure identical, which is the shape of a different drive
 * revision or measurement condition rather than a slip in a cell.
 *
 * **Neither number is a typo, and the page proves its own.** The footnote gives
 * the composition -- "Average read = Avg. Seek time + Avg. Latency + Sector
 * Time" -- and 27 + 8.33 is 35.33, the printed `35.3`. The same holds down the
 * column: the Micropolis's two rows print 28 and 23 against `36.3` and `31.3`.
 * So `002398-04`'s AVG column is the number its own AVG READ column was computed
 * from.
 *
 * **30 ms is kept**, on the rule this project already runs on rather than on a
 * count of documents. `008778-03` Table 6-5 is the *part's* approval table,
 * naming the exact Maxtor EXT-4380 and giving its four timings; `002398-04`
 * p. 6-3 is a summary across nineteen drives, in a chapter whose disk table one
 * page earlier repeats a single TOTAL BLOCKS value down three rows of different
 * geometries. Where the handbook disagrees with a part's own manual it has been
 * wrong before -- the disk mask register, the floppy reset polarity, the DMA
 * byte-pointer address -- and this is the same kind of table.
 *
 * The disagreement is 3 ms on a 38 ms access, and it is recorded rather than
 * split: a figure this core reports must come from one source that can be named,
 * not from an average of two. */
#define AP_OMTI_DRIVE_RPM 3600u
/* Table 6-5 "Nominal rpm 3600", and Table 6-7 "Rotational speed 3600 rpm
 * (+/- 0.5%), Index period 16.67 msec (+/- 0.5%)". Half a revolution is the
 * average rotational wait for a sector to arrive, and it comes out at 8.33 ms
 * -- which is Table 6-5's "Average latency" to the digit. */
#define AP_OMTI_ROTATION_TIME (AP_TIME_BASE_HZ * 60u / AP_OMTI_DRIVE_RPM)
#define AP_OMTI_AVERAGE_LATENCY (AP_OMTI_ROTATION_TIME / 2u)
/* Table 6-5's 1/3 stroke figure, against 4 ms single-cylinder and 58 ms
 * maximum stroke. */
#define AP_OMTI_AVERAGE_SEEK (AP_TIME_BASE_HZ / 1000u * 30u)
/* §6.2 and §6.3: the 155 Mbyte and 348 Mbyte drives "must be interface-
 * compatible with ESDI disk drives at a data transfer rate of 10.0
 * megabits/second (NRZ)". Ten megabits is 1.25 Mbyte/s, which is the figure
 * this core already carried -- confirmed rather than changed. Table 6-3/6-4/6-5
 * head their first row "Transfer rate (MB/second)" and print 5 and 10; the unit
 * in that heading is wrong and the prose is right, which the 72 Mbyte drive's
 * own numbers settle: 10,416 bytes per track at 3600 rpm is 625 Kbyte/s, and
 * that is 5 mega*bits*. */
#define AP_OMTI_TRANSFER_BYTES_PER_SEC 1250000u

/* ## The floppy drive's figures, which are a different drive's
 *
 * The floppy half of this board is a separate mechanism -- §5.4.1.2 has it
 * "independent of the Z8681 and ... operated entirely by the host CPU" -- and it
 * had **no access time at all** until `008778-03` chapter 7 was walked. That is
 * the same defect the fixed disk carried, and the fixed disk's is documented
 * above as not merely imprecise but fatal to Domain/OS.
 *
 * Chapter 7 is a drive specification, and every figure below is printed in it:
 *
 *     Table 7-1  Disk rotation speed        360 rpm (+/- 2%)
 *     Table 7-1  Average latency time      83.3 msec
 *     Table 7-1  Data transfer rate         500 Kbit/sec
 *     Table 7-7  Track-to-track time           3 msec minimum
 *     Table 7-7  Settling time                15 msec (excluding track-to-track)
 *     Table 7-7  Average track access time    94 msec (for 80 cylinders,
 *                                                      including settling)
 *
 * **The composition is checked against the document's own aggregate.** A seek
 * of `d` cylinders costs `d` steps plus one settle, and the mean of that over
 * an 80-cylinder drive is 94.8 ms against Table 7-7's published 94 -- 0.9%
 * apart, which is what a step-plus-settle model should give and is the evidence
 * that the three figures describe one mechanism rather than three unrelated
 * numbers. `omti_suite` asserts it. So unlike the fixed disk, whose head
 * position this core does not model, the floppy's seek is **distance-
 * dependent**: `fdc_cylinder[]` already tracks where each head is.
 *
 * Chapter 7 also confirms the format `image/ap_afd.h` took from the oracle:
 * §7.2's "high-density mode at 360 rpm, with a data transfer rate of 500
 * Kb/sec" and Table 7-1's MFM, 2 sides, 96 TPI are `apollo_dsk.cpp`'s
 * "FF_525, DSHD, MFM, 1200 (1 us, 360 rpm)" exactly. The one apparent
 * difference is explained rather than left open: Table 7-1 gives the **drive**
 * 80 cylinders and 160 tracks where the Apollo format uses 77, and §7.3 says
 * why -- "In the high density mode, the FDD is **equivalent to an 8-inch,
 * double-sided, double-density** flexible disk drive in data storage capacity
 * and data transfer rate". The medium is formatted to 8-inch equivalence on an
 * 80-cylinder mechanism, so 77 of 80 is the format and 80 is the drive.
 *
 * ## `002398-04` p. 6-3 confirms the composition from the outside
 *
 * Where the same page disagrees with `008778-03` about the *fixed* disk, its
 * floppy row corroborates this block in a way no single figure could. It reads
 * `T-to-T 3, AVG 77, MAX 231, RPM 360, AVG LATENCY 83.3, TRANSFER RATE .0625,
 * AVG READ 176.0`, and three of those are arithmetic on the numbers above:
 *
 *   - **MAX 231 is exactly 77 x 3 ms** -- a full-stroke seek over the *format's*
 *     77 cylinders at Table 7-7's track-to-track time, with no settle added.
 *   - **AVG 77 is exactly one third of that**, the uniformly-random mean of a
 *     step-per-cylinder seek, again without settling.
 *   - Adding Table 7-7's 15 ms settle gives 92 ms against its own published
 *     "average track access time 94 msec (including settling)" for 80
 *     cylinders. Two documents, two conventions, one mechanism.
 *
 * So the step-plus-settle model here is not merely consistent with the aggregate
 * it was built from -- a second document publishes the *unsettled* seek and it
 * is the step time times the cylinder count, which is what the model computes.
 *
 * `AVG READ 176.0` closes the loop on the sector size too: 77 + 83.3 leaves 15.7
 * ms of "Sector Time", and 1024 bytes at .0625 Mbyte/s is 16.4 ms. A 512-byte
 * sector would leave half that. `image/ap_afd.h`'s 77 x 2 x 8 x **1024** is the
 * geometry the page's own arithmetic requires.
 *
 * **Documented and deliberately not modelled**, each with its reason:
 *   - Head load time, Table 7-1's 35 msec. §7.7.5: "The *Domain System* does
 *     not require a head load solenoid." A drive without one loads no head, and
 *     charging for a mechanism the system specification excludes would be
 *     inventing time.
 *   - Start time, Table 7-1's 500 msec maximum to speed. `PROVISIONAL`: this
 *     core does not time the spindle from the Digital Output Register's motor
 *     bits, so a command issued into a stopped spindle completes as though the
 *     disk were at speed. What it would take to close it is a motor-on
 *     timestamp and a decision about what a too-early command *does* -- and
 *     since no manual here says whether that is an error or a wait, modelling
 *     it now would mean inventing a failure mode. Named in
 *     `docs/PROJECT_STATUS.md`.
 */
#define AP_OMTI_FDC_DRIVE_RPM 360u
#define AP_OMTI_FDC_ROTATION_TIME \
  (AP_TIME_BASE_HZ * 60u / AP_OMTI_FDC_DRIVE_RPM)
/* Half a revolution, which comes out at 83.33 ms -- Table 7-1's "Average
 * latency time 83.3 msec" to the digit, as the Winchester's did. */
#define AP_OMTI_FDC_AVERAGE_LATENCY (AP_OMTI_FDC_ROTATION_TIME / 2u)
#define AP_OMTI_FDC_TRACK_TO_TRACK (AP_TIME_BASE_HZ / 1000u * 3u)
#define AP_OMTI_FDC_SETTLING (AP_TIME_BASE_HZ / 1000u * 15u)
/* 500 Kbit/s is 62,500 bytes a second. */
#define AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC 62500u
/* Table 7-1's drive, which is not `AP_AFD_CYLINDERS`'s format. Used only by the
 * test that checks the composition against Table 7-7's published average. */
#define AP_OMTI_FDC_DRIVE_CYLINDERS 80u

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
  /* AT `3F7` written, which is **not** `3F6` and was sharing its byte.
   *
   * Table 4-3 gives `3F6` write as the Additional Control Register and `3F7`
   * write as the Diskette Control Register -- two registers, and this model put
   * both into `fdc_control`, so selecting a data rate silently cleared whatever
   * write precompensation had been programmed and reading either back gave the
   * other. Found by walking `002398-04` p. 12-14 against this file: the handbook
   * prints `3F6`'s fields, which is what made it visible that `3F7`'s writes
   * were landing on them.
   *
   * `[OMTI]` names the register and does not decompose it; the sibling 8640
   * manual's §5.1 does, and is the transcription used here as it is for the Main
   * Status Register: "an output only register which gives the controller data
   * rate information. All bits are cleared when a channel reset occurs. Bits
   * 7-2 Reserved", then bits 1 and 0 --
   *
   *     0 0   500 Kbits/sec
   *     0 1   300 Kbits/sec
   *     1 0   250 Kbits/sec
   *     1 1   Reserved
   *
   * Zero at reset is therefore **500 Kbit/s**, which is the rate this machine's
   * drive runs at: `008778-03` §7.2 requires it to "operate in high-density mode
   * at 360 rpm, with a data transfer rate of 500 Kb/sec", and
   * `AP_OMTI_FDC_TRANSFER_BYTES_PER_SEC` is that figure. So the register's
   * power-on value is already the only rate this drive has, which is why nothing
   * here is timed off it: selecting 300 or 250 would describe a medium the Apollo
   * drive does not take. The byte is kept and reported rather than acted on, and
   * `ap_omti_fdc_data_rate` says which of the four a driver asked for. */
  uint8_t fdc_rate;
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
  /* Set when SEEK and RECALIBRATE *arrive*, read and cleared by SENSE INTERRUPT
   * STATUS -- which is the only way a driver learns a seek finished. Arrival is
   * `fdc_seek_at` below, not the instant the command was issued. */
  bool fdc_seek_done;
  uint8_t fdc_seek_st0;

  /* ## The floppy's two deadlines
   *
   * The floppy half runs its own clock because §4.1 makes the two halves
   * independent and §3.4 has them running at once: a floppy seek must not
   * cancel a Winchester read, so nothing here may share `completion_at`.
   *
   * `fdc_completion_at` is the controller's: while it stands, `fdc_phase` is
   * `AP_OMTI_PHASE_EXECUTING`, the Main Status Register reads busy with `RQM`
   * down, and the result bytes are already prepared but not yet offered. It
   * carries the same named approximation the Winchester's `completion_at` does,
   * three fields above -- the data phase is not delayed, only the completion.
   *
   * `fdc_seek_at[unit]` is the *drive's*, one per drive, and it is why there are
   * two: `SEEK` and `RECALIBRATE` return the controller to idle at once -- §6.3
   * gives them no result phase -- while the head is still moving. The Main
   * Status Register's `SEEK_A` and `SEEK_B` are set while the deadline stands,
   * which is what "Drive A is in the Seek mode" means, and a second drive can be
   * seeking at the same time. `AP_TIME_NEVER` for a drive not seeking; a zero
   * would be a deadline in the past on a machine that has just started, and the
   * two must not be confusable. */
  ap_time_t fdc_completion_at;
  ap_time_t fdc_seek_at[2];

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
/* The earliest instant either of this controller's two lines could change by
 * time alone -- the conservative lower bound `board/ap_sio.h` states the rule
 * for. The only thing here that moves with time is a command's access time, and
 * `ap_omti_advance` returns immediately unless the controller is executing one,
 * so a controller in any other phase cannot change without a bus access. */
[[nodiscard]] ap_time_t ap_omti_interrupt_next_change(const ap_omti_t *omti);

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

/* The Additional Control Register's three fields and the Diskette Control
 * Register's data rate, as the driver last wrote them.
 *
 * They exist because the alternative is a register that is stored and never
 * read, which this file has already had to fix once -- the Digital Output
 * Register's motor bits, "defined and never read". None of the four changes any
 * timing here, for the reasons given beside their definitions; what an accessor
 * buys is that a driver's request is *observable* rather than absorbed. */
[[nodiscard]] uint8_t ap_omti_fdc_precompensation(const ap_omti_t *omti);
[[nodiscard]] bool ap_omti_fdc_control_pin(const ap_omti_t *omti, unsigned pin);
[[nodiscard]] ap_omti_fdc_rate_t ap_omti_fdc_data_rate(const ap_omti_t *omti);

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
