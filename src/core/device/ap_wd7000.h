/* Western Digital WD7000-ASC SCSI host adapter: the host interface.
 *
 * `[WD7000]` *WD7000-ASC Engineering Specification*, 96-000494 Rev. X3, August
 * 1988, walked whole in `docs/references/WD7000_WALK.md`. This is the DS5500's
 * SCSI controller, identified from `/install/ri.apollo.os.v.10.4/sau14/
 * scsi14.drvr` -- which names it in a string and drives it in code -- and
 * placed at **ISA `200`, physical `050000`-`050003`**, which is where `[GPIO]`
 * Table 3-1 puts the *Tape Controller*. Detail in `docs/PROJECT_STATUS.md`.
 *
 * ## What this file is, and what it is not
 *
 * **This is the host interface: the four registers, the command port, the
 * status and interrupt-status bytes, the reset and diagnostic sequence, the
 * mailbox arithmetic, the interrupt queue and the execution-parameter block.**
 * It is finished against `[WD7000]` chapter 5 and Appendix A.
 *
 * **It is not the SCSI bus.** Executing an SCB means arbitrating, selecting a
 * target, moving a CDB and data through first-party DMA and collecting a SCSI
 * status byte, and there is no target for it to talk to: this core models no
 * SCSI device. That is the same split the cartridge tape already has --
 * `ap_sc499.h` is the controller and `ap_qic.h` the drive -- and the target
 * layer is a named plan item rather than a gap in this part. Everything here
 * that would reach the bus is marked `PROVISIONAL` at its own site.
 *
 * ## The four registers, `[WD7000]` Table A-1
 *
 *   BASE+0  read  ASC Status         write  Command Register
 *   BASE+1  read  Host Interrupt Status   write  ASC Interrupt Acknowledge
 *                                                (a strobe; the value is
 *                                                 ignored)
 *   BASE+2  read  reserved           write  Host Control register, D3-D0
 *   BASE+3  reserved both ways
 *
 * ## The low nibble of the status port is not driven
 *
 * §5.2.1: on reset "the upper nibble clears and the lower nibble reads 1s",
 * because nothing drives D3-D0, and "the lower nibble of the status byte should
 * be masked off by all application drivers". So a reset part reads `0F`, a part
 * that has finished its diagnostics reads `4F`, and an initialised one `5F`.
 * `scsi14.drvr` masks `F0` and accepts `40`, `50`, `4F` and `5F`, which is
 * exactly this rule and the two states either side of initialization.
 *
 * ## `60` and `70` are the same rejection, and the document does not contradict
 *
 * §5.1.1 and §6.2.14.1 both say a rejected command byte posts **`60`**; §5.2.1
 * says **`70`**. The walk recorded that as a contradiction. It is not one:
 * `60` is `READY | REJECTED` and `70` is `READY | REJECTED | INITIALIZED`, so
 * the two pages differ only in whether initialization has happened yet -- and
 * both of the pages that say `60` are describing a rejected *initialization*
 * byte, which by definition arrives before the flag is set. The bit definitions
 * produce both values with no special case, and this part does.
 *
 * ## Reset, and why the pulse width is enforced
 *
 * §5.2.5.1: the host writes a 1 then a 0 to Host Control bit 0, "so the reset
 * pulse width is the host's to choose"; §5.1.1's table gives **minimum RESET
 * pulse width 25.0 us**. A narrower pulse is therefore not a reset, and this
 * part ignores one -- the same treatment `ap_sc499.h` gives `[SC499]` §1.12's
 * 25 us hold, and for the same reason: a stated minimum that a model does not
 * enforce is a documented behaviour the model does not have. `scsi14.drvr`
 * holds it for 500 ms, three orders over.
 *
 * ## Two diagnostics, and which one runs
 *
 * §6.2.14: "a power-up reset always runs the long walking-1s diagnostic and a
 * warm reset the short one", decided by a two-byte key the firmware leaves in
 * its own RAM. Long is "about 2 seconds" (§5.1.1), short "under 250 ms"
 * (§6.2.14.1). **No interrupt is raised either way** -- the host polls the
 * status port for `40` and then reads the interrupt-status register, which
 * §5.2.2 says "stays unchanged until the host's first command". Codes `05` and
 * `06`, the two D-FF tests, are "valid only after when ASC is first powered-up"
 * (Table 5-3), so a warm reset cannot produce them.
 *
 * ## The command port's 70 microseconds
 *
 * §5.2.1.2: writing the command port clears COMMAND PORT READY, and the ASC
 * sets it again "about 70 us" later, "or 35 us with a faster LCPU clock". The
 * 70 us figure is the one this part takes, since the Apollo board's LCPU speed
 * is not established here. §4.1 states the same interval as the guarantee the
 * host gets: "the host is told within 70 us whether the byte was accepted".
 */

#ifndef APOLLO_DEVICE_AP_WD7000_H
#define APOLLO_DEVICE_AP_WD7000_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_scsi.h"
#include "time/ap_time.h"

#define AP_WD7000_REGISTERS 4u

typedef enum {
  AP_WD7000_STATUS_COMMAND = 0u,  /* read status, write a command byte */
  AP_WD7000_INTSTAT_ACK = 1u,     /* read interrupt status, write acknowledges */
  AP_WD7000_CONTROL = 2u,         /* write the host control register */
  AP_WD7000_RESERVED = 3u,        /* reserved both ways */
} ap_wd7000_reg_t;

/* ASC status byte, `[WD7000]` Table 5-2 and Table A-9. */
#define AP_WD7000_ST_INTERRUPT 0x80u   /* D7 interrupt image flag */
#define AP_WD7000_ST_READY 0x40u       /* D6 command port ready */
#define AP_WD7000_ST_REJECTED 0x20u    /* D5 command byte rejected */
#define AP_WD7000_ST_INITIALIZED 0x10u /* D4 ASC initialized */
/* D3-D0 "reserved, set to 1, mask off this bit" -- nothing drives them. */
#define AP_WD7000_ST_UNDRIVEN 0x0Fu

/* Host control register, Table 5-4 and Table A-2, write only. */
#define AP_WD7000_CTL_ASC_RESET 0x01u  /* D0 */
#define AP_WD7000_CTL_SCSI_RESET 0x02u /* D1 */
#define AP_WD7000_CTL_DMA_ENABLE 0x04u /* D2, 0 = DRQ disabled */
#define AP_WD7000_CTL_IRQ_ENABLE 0x08u /* D3 */
/* D7-D4 "reserved, not used". "On power-up Reset or RESET DRV, all registers
 * are cleared to all zeros", and a driver should mask the upper nibble. */
#define AP_WD7000_CTL_UNUSED 0xF0u

/* Interrupt status byte / diagnostic error code, Table 5-3 and Table A-10.
 * The two share one register because the events are mutually exclusive: the
 * diagnostic code is valid once READY is asserted after a reset and until the
 * host's first command. */
#define AP_WD7000_DIAG_POWER_ON 0x00u /* no diagnostics executed */
#define AP_WD7000_DIAG_OK 0x01u
#define AP_WD7000_DIAG_RAM 0x02u
#define AP_WD7000_DIAG_FIFO 0x03u
#define AP_WD7000_DIAG_SBIC 0x04u
#define AP_WD7000_DIAG_INIT_DFF 0x05u /* first power-up only */
#define AP_WD7000_DIAG_IRQ_DFF 0x06u  /* first power-up only */
#define AP_WD7000_DIAG_ROM 0x07u
/* Appendix A.7 adds one the body's table does not carry. */
#define AP_WD7000_DIAG_DMA 0x18u /* DMA error during host memory diagnostics */

/* `10NNNNNN` and `11NNNNNN`: the mailbox forms of the same register. */
#define AP_WD7000_INT_OGMB_FREE 0x80u
#define AP_WD7000_INT_ICMB_SERVICE 0xC0u
#define AP_WD7000_INT_BOX_MASK 0x3Fu
#define AP_WD7000_INT_FORM_MASK 0xC0u

/* Command port opcodes, Table 6-1 and Table A-3. */
#define AP_WD7000_CMD_NOP 0x00u
#define AP_WD7000_CMD_INITIALIZE 0x01u /* a ten-byte sequence */
#define AP_WD7000_CMD_DISABLE_UNSOLICITED 0x02u
#define AP_WD7000_CMD_ENABLE_UNSOLICITED 0x03u
#define AP_WD7000_CMD_INT_ON_FREE_OGMB 0x04u
#define AP_WD7000_CMD_SCSI_SOFT_RESET 0x05u /* a two-byte sequence */
#define AP_WD7000_CMD_SCSI_HARD_RESET_ACK 0x06u
/* `07`-`7F` reserved. `10NNNNNN` starts the command in OGMB N, `11NNNNNN`
 * scans the mailboxes with N as a six-bit signature. */
#define AP_WD7000_CMD_START_OGMB 0x80u
#define AP_WD7000_CMD_SCAN 0xC0u

/* The ten bytes of the initialization sequence, Table 6-2 and Table A-4. */
#define AP_WD7000_INIT_BYTES 10u
/* The two bytes of the soft-reset sequence, Table 6-3. */
#define AP_WD7000_SOFT_RESET_BYTES 2u
/* Soft reset parameter: bit 3 selects the message sent to the target. */
#define AP_WD7000_SOFT_RESET_DEVICE_RESET 0x08u /* 0 = ABORT, 1 = BUS DEVICE RESET */

/* ICB command opcodes, Table 6-4 and Table A-7. Carried here because the host
 * interface must reject an opcode outside the set, which is what `[WD7000]`
 * §5.2.1.3 calls "illegal command or parameter". */
#define AP_WD7000_ICB_OPEN_INBOUND 0x80u
#define AP_WD7000_ICB_RECEIVE_COMMAND 0x81u
#define AP_WD7000_ICB_RECEIVE_DATA 0x82u
#define AP_WD7000_ICB_RECEIVE_DATA_STATUS 0x83u
#define AP_WD7000_ICB_SEND_DATA 0x84u
#define AP_WD7000_ICB_SEND_DATA_STATUS 0x85u
#define AP_WD7000_ICB_SEND_STATUS 0x86u
#define AP_WD7000_ICB_OPEN_SCRATCHPAD 0x87u
#define AP_WD7000_ICB_READ_INIT_BYTES 0x88u
#define AP_WD7000_ICB_READ_DEVICE_ADDRESS 0x89u
#define AP_WD7000_ICB_SET_PARAMETERS 0x8Au
#define AP_WD7000_ICB_READ_PARAMETERS 0x8Bu
#define AP_WD7000_ICB_READ_FIRMWARE 0x8Cu
#define AP_WD7000_ICB_EXECUTE_DIAGNOSTICS 0x8Du
#define AP_WD7000_ICB_PREPARE_SENSE 0x8Eu
#define AP_WD7000_ICB_PREPARE_INQUIRY 0x8Fu
#define AP_WD7000_ICB_OPEN_OUTBOUND 0x90u
#define AP_WD7000_ICB_SET_BUS_TIMES 0x91u

/* ICMB return status, Table A-11. */
#define AP_WD7000_ICMB_COMPLETE 0x01u
#define AP_WD7000_ICMB_COMPLETE_ERROR 0x02u
#define AP_WD7000_ICMB_SCAN_COMPLETE 0x03u
#define AP_WD7000_ICMB_NO_SCSI_STATUS 0x04u
#define AP_WD7000_ICMB_BUS_RESET_DURING 0x05u
#define AP_WD7000_ICMB_HARDWARE_FAILURE 0x06u
#define AP_WD7000_ICMB_SOFT_RESET_DONE 0x07u
#define AP_WD7000_ICMB_UNEXPECTED_RESELECTION 0x80u
#define AP_WD7000_ICMB_UNEXPECTED_SELECTION 0x81u
#define AP_WD7000_ICMB_ABORT_MESSAGE 0x82u
#define AP_WD7000_ICMB_RESET_MESSAGE 0x83u
#define AP_WD7000_ICMB_HARD_RESET_IDLE 0x84u

/* Vendor unique error codes, Appendix A.7. The illegal-parameter band is the
 * one the host interface itself can raise; the hardware and protocol bands
 * belong to the bus half. */
#define AP_WD7000_VUE_OGMB_EMPTY 0x20u
#define AP_WD7000_VUE_ILLEGAL_PARAMETER 0x21u
#define AP_WD7000_VUE_CANNOT_EXECUTE_NOW 0x22u
#define AP_WD7000_VUE_WRONG_DIRECTION 0x23u
#define AP_WD7000_VUE_INDEX_OUT_OF_RANGE 0x24u
#define AP_WD7000_VUE_COUNT_OUT_OF_RANGE 0x25u
#define AP_WD7000_VUE_COMMAND_IN_PROGRESS 0x26u
/* §A.7's SCSI-protocol band, the three this part posts. `40` is explicitly a
 * *warning*: "target sent less than the allocation length". */
#define AP_WD7000_VUE_SHORT_TRANSFER 0x40u
#define AP_WD7000_VUE_SELECTION_TIMEOUT 0x4Du
#define AP_WD7000_VUE_NONE 0xFFu

/* Mailboxes, §5.3 and Table A-8. Four bytes each: a status byte then a
 * three-byte command-block pointer, MSB first. All the outgoing boxes are
 * contiguous and all the incoming ones follow them; the counts are
 * independent, and zero or one both mean one. */
#define AP_WD7000_MAILBOX_BYTES 4u
#define AP_WD7000_MAILBOXES_MAX 64u
/* The ASC's own interrupt queue: "up to 32 IRQs can be queued internally to
 * the ASC" (Table A-8's note; §5.5 calls the same queue 32 deep). */
#define AP_WD7000_IRQ_QUEUE 32u
/* Command, SBIC and completion queues, §5.5, and the SCB store behind them. */
#define AP_WD7000_COMMAND_QUEUE 16u
#define AP_WD7000_THREADS 16u
#define AP_WD7000_SCB_BYTES 32u
#define AP_WD7000_ICB_BYTES 16u

/* The execution parameter block, §6.2.11 and Appendix A.8: 26 bytes, indexed
 * 0-25, every default printed. */
#define AP_WD7000_PARAMETERS 26u
#define AP_WD7000_PARAM_SYNC_FIRST 0u   /* 0-15, even requested / odd agreed */
#define AP_WD7000_PARAM_SYNC_COUNT 16u
#define AP_WD7000_PARAM_SBIC_CONTROL 17u
#define AP_WD7000_PARAM_TIMEOUT 18u
#define AP_WD7000_PARAM_SOURCE_ID 21u
#define AP_WD7000_PARAM_USER_FLAGS 22u
#define AP_WD7000_PARAM_UNSOLICITED_MASK 23u
#define AP_WD7000_PARAM_PARITY_RETRIES 24u

#define AP_WD7000_SYNC_DEFAULT 0x40u       /* asynchronous */
#define AP_WD7000_SYNC_RECOMMENDED 0x45u
#define AP_WD7000_SYNC_NEGOTIATED 0x80u    /* bit 7, set once a rate is agreed */
#define AP_WD7000_SBIC_CONTROL_DEFAULT 0x0Cu
#define AP_WD7000_SBIC_CONTROL_PARITY 0x01u /* the only bit a host may set */
#define AP_WD7000_TIMEOUT_DEFAULT 0x19u    /* about 250 ms */
#define AP_WD7000_SOURCE_ID_DEFAULT 0x80u
#define AP_WD7000_SOURCE_ID_RESELECTION 0x80u /* target may disconnect */
#define AP_WD7000_SOURCE_ID_SELECTION 0x40u   /* other initiators may select */
#define AP_WD7000_USER_FLAG_RESIDUAL 0x04u    /* bytes moved into SCB 16-18 */
#define AP_WD7000_USER_FLAG_FREE_OGMB 0x02u
#define AP_WD7000_USER_FLAG_UNSOLICITED 0x01u
#define AP_WD7000_PARITY_RETRIES_DEFAULT 0x02u
/* The unsolicited mask's bits, §6.2.11.9. Bits 0-4 produce interrupts `80`-`84`;
 * bit 5 produces ICMB0 `81` with vue `85`. */
#define AP_WD7000_UNSOL_RESELECTION 0x01u
#define AP_WD7000_UNSOL_SELECTION 0x02u
#define AP_WD7000_UNSOL_ABORT 0x04u
#define AP_WD7000_UNSOL_DEVICE_RESET 0x08u
#define AP_WD7000_UNSOL_BUS_RESET 0x10u
#define AP_WD7000_UNSOL_REQUEST_SENSE 0x20u

/* The ASC's own SCSI ID is three bits, §6.1.2 byte 01. */
#define AP_WD7000_SCSI_ID_MASK 0x07u
/* The DMA bus on and off times count in 125 ns, §6.1.2 bytes 02 and 03. */
#define AP_WD7000_BUS_TIME_NS 125u

/* Derived from the time base, never written down as a unit count -- the same
 * rule `ap_sc499.h` follows and for the same reason. */
#define AP_WD7000_US(n) ((ap_time_t)(AP_TIME_BASE_HZ / 1000000u) * (n))

/* §5.2.1.2 and §4.1: "about 70 us", or 35 with a faster LCPU clock. */
#define AP_WD7000_T_COMMAND_PORT AP_WD7000_US(70)
/* §5.1.1's table: minimum RESET pulse width. */
#define AP_WD7000_T_RESET_MIN AP_WD7000_US(25)
/* §6.2.14.1: "short diagnostics complete in under 250 ms". A *bound*, taken as
 * the value under `CLAUDE.md`'s rule for a quantity published as a range, and
 * therefore **PROVISIONAL** -- named in `docs/PROJECT_STATUS.md`. */
#define AP_WD7000_T_SHORT_DIAGNOSTIC AP_WD7000_US(250000)
/* §5.1.1: "on-board diagnostics run for about 2 seconds". Also **PROVISIONAL**,
 * for the same reason: "about" is not a value. */
#define AP_WD7000_T_LONG_DIAGNOSTIC AP_WD7000_US(2000000)

/* What the command port is in the middle of, if anything. A sequence is one
 * opcode plus a fixed number of parameter bytes, and §5.2.1.2 makes each byte
 * its own handshake. */
typedef enum {
  AP_WD7000_SEQ_NONE = 0,
  AP_WD7000_SEQ_INITIALIZE,
  AP_WD7000_SEQ_SOFT_RESET,
} ap_wd7000_sequence_t;

/* ---- The SCB, `[WD7000]` Table 5-6 and Table A-5 -------------------------- */

/* Thirty-two bytes, and **the offsets are decimal**: the table numbers them
 * `00` through `31` and the CDB occupies `02`-`13`, which is twelve bytes only
 * if those are decimal. Read as hex the CDB would be eighteen and the block
 * would not close at 32. */
#define AP_WD7000_SCB_OPCODE 0u
#define AP_WD7000_SCB_TARGET 1u
#define AP_WD7000_SCB_CDB 2u
#define AP_WD7000_SCB_STATUS 14u
#define AP_WD7000_SCB_VUE 15u
#define AP_WD7000_SCB_MAX_LENGTH 16u   /* 16-18, MSB first */
#define AP_WD7000_SCB_DATA_POINTER 19u /* 19-21, MSB first */
#define AP_WD7000_SCB_LINK 22u         /* 22-24, MSB first */
#define AP_WD7000_SCB_DIRECTION 25u
/* `26`-`31` are "reserved, to be zeroed" and are not read. */

/* Byte 00: "`00` means a SCSI command with the ASC as initiator, `01`-`7F`
 * reserved, `80`-`FF` ASC command codes". */
#define AP_WD7000_SCB_INITIATOR_COMMAND 0x00u

/* Byte 01: "Target ID in bits 7-5, two zero bits, LUN in bits 2-0". */
#define AP_WD7000_SCB_TARGET_SHIFT 5u
#define AP_WD7000_SCB_TARGET_MASK 0x07u
#define AP_WD7000_SCB_LUN_MASK 0x07u

/* Byte 25 bit 7: "direction, set when data is written to the host, so every
 * SCSI read sets it and every write clears it". */
#define AP_WD7000_SCB_TO_HOST 0x80u

/* Table 5-5: an OGMB status of zero means the ASC has taken the mail, non-zero
 * means full. So the ASC clears it when it takes the block, and a start
 * command naming a box that already reads zero is vue `20`, "command issued
 * with the OGMB marked empty". */
#define AP_WD7000_MAILBOX_EMPTY 0x00u

/* An ICMB's first byte is the completion code and the next three the CDB
 * address, "`FFFFFF` when meaningless". */
#define AP_WD7000_ICMB_NO_ADDRESS 0xFFFFFFu

/* ---- First-party DMA, `[WD7000]` §3 and Appendix C ----------------------- */

/* The ASC is a **bus master**: it reads the SCB, the CDB's data buffer and the
 * mailboxes out of host memory itself, and the AT's 8237 "does arbitration
 * only" with its channel in **cascade mode** and its mask cleared. So this part
 * needs a way to reach memory that is not a DMA controller, and this is it.
 *
 * Appendix C's sequencing rule is the host's, not ours: because the ASC
 * tri-states DRQn whenever Host Control bit 2 is clear, the 8237 channel must
 * be masked *before* the card's DMA is enabled or disabled "or the system is
 * likely to hang". `ap_wd7000_drq_driven` already reports that line; nothing
 * here enforces a rule about the host's own controller.
 *
 * **Addresses are AT bus addresses, not physical ones.** `019411-A00` §4.2.1.4:
 * the address translation map "provides a 512-KB window through which external
 * AT compatible bus masters can access CPU main memory". The board's hook is
 * what applies it -- `AP_ATMAP_TRANSFER_BUS_MASTER` -- and this part passes the
 * address through as the SCB gave it. */
typedef struct {
  void *context;
  uint8_t (*read)(void *context, uint32_t address);
  void (*write)(void *context, uint32_t address, uint8_t value);
} ap_wd7000_memory_t;

typedef struct {
  /* Host-visible state. */
  bool ready;        /* status D6 */
  bool rejected;     /* status D5 */
  bool initialized;  /* status D4 */
  bool interrupt;    /* status D7, the image of the hardwired line */
  uint8_t control;   /* the host control register, as written */
  uint8_t int_status; /* the interrupt status / diagnostic code register */

  /* The clock. Carried by the device for the same reason `ap_sc499_t` carries
   * one: `ap_board_write` has no `now` to hand a register write. */
  ap_time_t now;

  /* Reset. `held` dates the rising edge of control bit 0 so the falling edge
   * can measure the pulse against §5.1.1's 25 us minimum; the dating happens
   * at the next advance because the write itself has no instant to read. */
  bool in_reset;
  bool hold_dating;
  bool hold_dated;
  ap_time_t held_since;

  /* Diagnostics. `powered_on` is the two-byte RAM key of §6.2.14 -- false
   * until the first diagnostic has run, which is what makes that one the long
   * walking-1s test and every later one the short test. */
  bool powered_on;
  bool diagnosing;
  ap_time_t diagnose_at;
  bool led;             /* §A.9 port 27: 1 = test failed */

  /* The command port's 70 us. */
  bool busy;
  ap_time_t ready_at;

  /* A multi-byte sequence in progress. */
  ap_wd7000_sequence_t sequence;
  unsigned taken;
  uint8_t parameter[AP_WD7000_INIT_BYTES];

  /* What the initialization command established, §6.1.2. */
  uint8_t scsi_id;
  uint8_t bus_on;
  uint8_t bus_off;
  uint32_t mail_base;
  unsigned ogmb_count;
  unsigned icmb_count;

  /* The execution parameter block, Appendix A.8. */
  uint8_t parameters[AP_WD7000_PARAMETERS];

  /* The ASC's interrupt queue, Table A-8's note. Entries are whole interrupt
   * status bytes, so the queue holds both mailbox forms and the diagnostic
   * codes without a discriminator. */
  uint8_t queue[AP_WD7000_IRQ_QUEUE];
  unsigned queue_head;
  unsigned queue_count;
  /* Whether the host has acknowledged the interrupt it was last shown. §5.2.4:
   * "the ASC will not raise the next interrupt until the previous one is
   * acknowledged". */
  bool awaiting_ack;

  /* §6.1.5: one-shot, "used only when Interrupt on Free OGMB has been issued". */
  bool interrupt_on_free_ogmb;
  /* §6.1.7: a hard reset acknowledged puts the ASC in "a pseudo idle loop"
   * accepting only reset and abort commands until a second `06` releases it. */
  bool pseudo_idle;
  /* A SCSI bus reset the host asked for through control bit 1. */
  bool scsi_reset;

  /* The bus, absent until a board attaches one. A part with no bus answers
   * every start command with vue `4D` -- which is what an ASC with nothing
   * cabled to it does. */
  struct ap_scsi_bus *bus;
  /* What the SCB path has done, for the boot report. */
  uint32_t scbs_started;
  uint32_t scbs_empty;      /* vue `20`: the box was already taken */
  uint32_t scbs_unsupported; /* byte 00 was not `00`: an ICB, not a SCSI command */
  uint32_t scan_signatures;

  /* First-party DMA. Absent until a board attaches one, which is what makes a
   * part built by a suite unable to touch memory it was never given. */
  ap_wd7000_memory_t memory;
  bool memory_attached;
  uint32_t dma_reads;
  uint32_t dma_writes;
  /* Accesses refused because Host Control bit 2 was clear. `[WD7000]` §5.2.5.3:
   * the DRQ line is tri-stated then, so a master cycle cannot start -- counted
   * rather than asserted, because a host that has not enabled DMA yet is in an
   * ordinary state and not an error. */
  uint32_t dma_refused;
} ap_wd7000_t;

/* Power-on. Clears everything, including `powered_on`, so the first diagnostic
 * is the long one. */
void ap_wd7000_power_on(ap_wd7000_t *asc);

/* An ASC reset: §5.2.5.1's falling edge, or RESET DRV. Keeps `powered_on`, so
 * the diagnostic that follows is the short one. */
void ap_wd7000_reset(ap_wd7000_t *asc);

/* The register interface. `reg` is an `ap_wd7000_reg_t`. */
uint8_t ap_wd7000_read(ap_wd7000_t *asc, unsigned reg);
void ap_wd7000_write(ap_wd7000_t *asc, unsigned reg, uint8_t value);

/* Advance the device's own clock and retire anything that has come due. */
void ap_wd7000_advance(ap_wd7000_t *asc, ap_time_t now);

/* The two tri-stated lines. §5.2.5.3 and §5.2.5.4: each is driven only while
 * its enable bit is set, "so that several cards can share a channel", and
 * Appendix C warns that the host must mask its own 8237 channel and 8259 line
 * before either is enabled or disabled. */
bool ap_wd7000_irq(const ap_wd7000_t *asc);
bool ap_wd7000_drq_driven(const ap_wd7000_t *asc);

/* Mailbox addressing, Table A-8. Both return the host physical address of the
 * four-byte box; `n` and `m` are offsets from zero. A box outside the
 * configured count has no address and these return zero, which is not a legal
 * mail block base. */
uint32_t ap_wd7000_ogmb_address(const ap_wd7000_t *asc, unsigned n);
uint32_t ap_wd7000_icmb_address(const ap_wd7000_t *asc, unsigned m);

/* Queue an interrupt status byte. Used by the command port for the interrupts
 * it raises itself, and by the bus half for command completions when there is
 * one. Returns false when the queue is full, which §5.5's flowchart B-7 treats
 * by marking the spot and retrying. */
bool ap_wd7000_post_interrupt(ap_wd7000_t *asc, uint8_t status);

/* Give the part a bus to drive. A board does this once at attach. */
void ap_wd7000_attach_bus(ap_wd7000_t *asc, struct ap_scsi_bus *bus);

/* Execute the SCB one OGMB points at: `[WD7000]` §6.1.8's `10NNNNNN`. Reads the
 * mailbox, fetches the block, runs it on the bus, writes back bytes 14 and 15,
 * frees the mailbox and posts an ICMB. Returns false when the box was empty --
 * which is vue `20` and an ICMB of its own, not a silent no-op. */
bool ap_wd7000_start_ogmb(ap_wd7000_t *asc, unsigned n);

/* §6.1.9's `11NNNNNN`: scan every OGMB from the base address and start each
 * full one, then raise the scan's own completion. */
unsigned ap_wd7000_scan(ap_wd7000_t *asc, uint8_t signature);

/* Give the part a way to reach host memory. A board does this once at attach;
 * a part with none refuses every access and counts it. */
void ap_wd7000_attach_memory(ap_wd7000_t *asc, const ap_wd7000_memory_t *memory);
[[nodiscard]] bool ap_wd7000_memory_attached(const ap_wd7000_t *asc);

/* One byte of host memory, as a bus master. `ok` reports whether the cycle
 * happened at all: it does not when no memory is attached, and it does not when
 * Host Control bit 2 is clear. */
uint8_t ap_wd7000_memory_read(ap_wd7000_t *asc, uint32_t address, bool *ok);
void ap_wd7000_memory_write(ap_wd7000_t *asc, uint32_t address, uint8_t value,
                            bool *ok);

/* The three-byte pointers the SCB and the mailboxes are made of. `[WD7000]`
 * §5.3.2.1: "multi-byte parameter fields are MSB first", which Table A-8's own
 * mailbox layout and Table 6-2's mail-block address both follow -- and which
 * Table A-4 contradicts once, recorded in `WD7000_WALK.md` and resolved three
 * against one. */
[[nodiscard]] uint32_t ap_wd7000_memory_read24(ap_wd7000_t *asc,
                                               uint32_t address, bool *ok);
void ap_wd7000_memory_write24(ap_wd7000_t *asc, uint32_t address,
                              uint32_t value, bool *ok);

/* The status byte as the host would read it, without the side effects of a
 * read. Exposed so a report can print it. */
uint8_t ap_wd7000_status(const ap_wd7000_t *asc);

#endif /* APOLLO_DEVICE_AP_WD7000_H */
