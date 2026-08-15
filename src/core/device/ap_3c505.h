/* 3Com EtherLink Plus (3C505): the host-adapter interface.
 *
 * `[DEV]` *EtherLink Plus Developer's Guide*, 3Com, May 1986. Findings and
 * citations are in `docs/references/ETHERNET.md`; this header states the model
 * and points there rather than restating the evidence.
 *
 * ## What this models, and what it deliberately does not
 *
 * The card is an **intelligent adapter**: an Intel 80186 running firmware from
 * on-board ROM, with an 82586 LAN coprocessor doing the wire work (`[DEV]`
 * §1.2, §1.4, §1.5). The host never touches the 82586. So what a host-side
 * emulation must model is the **mailbox** -- five registers in sixteen I/O
 * locations -- and *not* an Ethernet controller, which is the shape a reader
 * expecting `ap_ring_*` would look for and not find.
 *
 * Whether the adapter's firmware is emulated behind that mailbox, or replaced
 * by a host-side implementation of the PCB protocol, is a separate decision and
 * is not made here. The register interface is the same either way, which is why
 * it is worth building first.
 *
 * ## The map, `[HIS]` §2-3 and §3-1
 *
 *     +0  Host Command Register      full duplex, byte wide, read and write
 *     +2  read:  Host Status Register
 *     +2  write: Host Aux DMA Register
 *     +4  Data Register              20-byte half duplex FIFO
 *     +6  Host Control Register      write; also readable on Rev 3 hardware
 *
 * `+2` is **two different registers by direction**, which is the shape that
 * catches a reader out and is the manual's.
 *
 * **This corrects `[DEV]` §1.3.3, which this header used to follow.** That
 * table puts the Host Control Register at `+2` on a write and readable at `+6`,
 * and it contradicts `[DEV]`'s *own* register summary in §2.1 -- which gives
 * Control at host `6` and AUX DMA at host `2`, write only -- and §2.5, which is
 * titled "Host Aux DMA Register". `[HIS]`, three years later, prints the same
 * summary twice (§2-3 as an address list, §3-1 as an offset table) and agrees
 * with §2.1 both times. So the model that read `+2` back as a control register
 * would have been reading the DMA burst control the host had written, and a
 * driver's read-modify-write of `HCR` would have gone to the wrong register
 * entirely. Found by the sibling-manual step, on a document that had been on
 * disk for a day.
 *
 * `[HIS]` §3-1's footnote is why `+6` is not simply "read/write": the Host
 * Control Register is **write-only on Rev 2 hardware**, and readable only on
 * Rev 3, which has the large gate array. A model must therefore decide which
 * revision it is; the DN3500's card is not yet established either way.
 *
 * Base is jumpered; the factory setting is `300H`, which through this machine's
 * AT decode -- `physical = 0x040000 + (ISA << 7)` -- puts the card at physical
 * `058000`. That agrees with what `ap_board.h` already records for it, from the
 * manual rather than from the oracle.
 *
 * ## The one thing a model must not do
 *
 * `[DEV]` §1.9.5: the adapter's `ASF1`-`ASF3` and the host's `HSF1`-`HSF2` are
 * general-purpose flags, and "they are not decoded by the hardware in any way".
 * They are firmware-to-driver convention. **A model passes them through and
 * interprets none of them** -- inventing a meaning for a flag the hardware
 * ignores would make a driver that works on real silicon fail here, and the
 * sentence exists in the manual precisely because the temptation is obvious.
 *
 * ## What is not yet known
 *
 * §1.9 defers the bit-level layout to a *3C505 Hardware Interface
 * Specification* this project does not hold. So the eleven flag **names** are
 * evidenced and their **positions** are not, and this header declares only what
 * the manual gives. Positions come from `[HIS]` if it can be found, or from the
 * oracle -- which is legitimate here, unlike for the ring, because MAME has a
 * runnable 3c505 and the document has been read first and found wanting.
 */

#ifndef APOLLO_DEVICE_AP_3C505_H
#define APOLLO_DEVICE_AP_3C505_H

#include <stdbool.h>
#include <stdint.h>

/* `[DEV]` §1.3.3: sixteen I/O locations from a jumpered base, factory `300H`. */
#define AP_3C505_IO_BASE_DEFAULT 0x300u
#define AP_3C505_IO_SIZE 16u

/* Offsets from the base, `[HIS]` §2-3 and §3-1. `+2` names two registers, by
 * direction. */
#define AP_3C505_REG_COMMAND 0u  /* read and write */
#define AP_3C505_REG_STATUS 2u   /* read: Host Status Register */
#define AP_3C505_REG_AUX_DMA 2u  /* write: Host Aux DMA Register */
#define AP_3C505_REG_DATA 4u     /* read and write */
#define AP_3C505_REG_CONTROL 6u  /* write, and read on Rev 3 hardware */

/* `[DEV]` §1.9.2: "a half duplex 20 byte FIFO". */
#define AP_3C505_DATA_FIFO 20u

/* `[DEV]` §3.1: the Primary Command Block the adapter idles waiting for --
 * a command byte, a length byte, then data. "The maximum PCB size the Adapter
 * can accept in this version ROM is 64 bytes", and the length "does not include
 * the PCB command code or the length field itself", so the data field is at
 * most 62. */
#define AP_3C505_PCB_MAX 64u
#define AP_3C505_PCB_DATA_MAX 62u

/* ## The four flag registers, `[HIS]` §3-2, §3-3, §3-5 and §3-6
 *
 * `[DEV]` §1.9 names the flags and defers their positions to a *3C505 Hardware
 * Interface Specification*; that document is `[HIS]`, and it has been found.
 * These are read from its page images -- every table is drawn most-significant
 * bit leftmost, as `CMD7 … CMD0` on §3-1 establishes -- so `bit 7` below is the
 * leftmost cell of each row.
 *
 * The pairing is the part worth stating, because it is what "general purpose,
 * not decoded by the hardware" means concretely: **each side writes its flags
 * into its own control register and reads the other side's out of its own
 * status register.** `HSF1`/`HSF2` are written by the host in `HCR` and appear
 * to the adapter in `ASR`; `ASF1`-`ASF3` are written by the adapter in `ACR`
 * and appear to the host in `HSR`. A model passes them through and interprets
 * none of them.
 *
 * The command-register handshake flags are likewise mirrored, and their names
 * are a trap: the *host*'s status register carries `HCRE` and `ACRF`, and the
 * *adapter*'s carries `ACRE` and `HCRF`. Each side is told whether its own
 * outgoing byte has been taken and whether an incoming one is waiting.
 */

/* Host Control Register, host `+6`. Written by the host. */
#define AP_3C505_HCR_HSF1 0x01u /* host status flag 1, seen by the adapter */
#define AP_3C505_HCR_HSF2 0x02u /* host status flag 2, seen by the adapter */
#define AP_3C505_HCR_CMDE 0x04u /* command register interrupt enable */
#define AP_3C505_HCR_TCEN 0x08u /* terminal count interrupt enable */
#define AP_3C505_HCR_DIR 0x10u  /* clear host->adapter, set adapter->host */
#define AP_3C505_HCR_DMAE 0x20u /* DMA enable */
#define AP_3C505_HCR_FLSH 0x40u /* flush the data register FIFO */
#define AP_3C505_HCR_ATTN 0x80u /* attention: NMI to the adapter's 80186 */
/* `[HIS]` §3-2: `ATTN` alone is a soft reset, and `ATTN` **and** `FLSH`
 * together are decoded by the hardware as a hard reset -- 80186, 82586, both
 * status registers, both control registers and the FIFO. The machine stays in
 * reset until both bits are cleared, so this is a level, not an edge. */
#define AP_3C505_HCR_HARD_RESET (AP_3C505_HCR_ATTN | AP_3C505_HCR_FLSH)

/* Host Status Register, host `+2` on a read. Written by the hardware. */
#define AP_3C505_HSR_ASF1 0x01u /* adapter status flag 1, from `ACR` */
#define AP_3C505_HSR_ASF2 0x02u /* adapter status flag 2, from `ACR` */
#define AP_3C505_HSR_ASF3 0x04u /* adapter status flag 3, from `ACR` */
#define AP_3C505_HSR_DONE 0x08u /* DMA terminal count reached */
#define AP_3C505_HSR_DIR 0x10u  /* the direction the host set in `HCR` */
#define AP_3C505_HSR_ACRF 0x20u /* adapter command register full: a byte waits */
#define AP_3C505_HSR_HCRE 0x40u /* host command register empty: send another */
#define AP_3C505_HSR_HRDY 0x80u /* data register ready, in the current direction */

/* Host Aux DMA Register, host `+2` on a **write**. `[HIS]` p. 3-5, read from
 * the page image: seven cells printed `0` and one named `BRST`, so bit 0 is the
 * whole of the register. "This register is cleared upon power-up. It doesn't
 * exist on older Rev 2 hardware boards." */
#define AP_3C505_AUX_BRST 0x01u /* DMA burst: no demand-mode pause */

/* Demand mode's pause, and the one number in it. `[DEV]` §1.9.4 and `[HIS]`
 * p. 3-5 state it in the same words from three years apart: "If the Burst bit
 * is not set, demand mode DMA transfers by the host will pause every 9
 * transfers to allow the PC to refresh its dynamic RAMs." Both add that the bit
 * "has no effect during single cycle DMA transfers", which is why this is a
 * property of the request line and not of the transfer. */
#define AP_3C505_DMA_DEMAND_BURST 9u

/* Adapter Control Register, adapter `+3` on a write and `+2` on a read.
 * Written by the adapter's firmware; the host never touches it. */
#define AP_3C505_ACR_ASF1 0x01u /* adapter status flag 1, seen by the host */
#define AP_3C505_ACR_ASF2 0x02u /* adapter status flag 2, seen by the host */
#define AP_3C505_ACR_ASF3 0x04u /* adapter status flag 3, seen by the host */
#define AP_3C505_ACR_LED1 0x08u /* set lights LED 1 */
#define AP_3C505_ACR_LED2 0x10u /* set lights LED 2 */
#define AP_3C505_ACR_R586 0x20u /* hold the 82586 in reset */
#define AP_3C505_ACR_FLSH 0x40u /* flush the data register FIFO */
#define AP_3C505_ACR_LPBK 0x80u /* clear enables loopback at the 8023 */

/* Adapter Status Register, adapter `+3` on a read. */
#define AP_3C505_ASR_HSF1 0x01u /* host status flag 1, from `HCR` */
#define AP_3C505_ASR_HSF2 0x02u /* host status flag 2, from `HCR` */
#define AP_3C505_ASR_SWTC 0x04u /* the TEST jumper's state */
#define AP_3C505_ASR_8_16 0x08u /* set: the card is in a 16-bit slot */
#define AP_3C505_ASR_DIR 0x10u  /* the direction the host set in `HCR` */
#define AP_3C505_ASR_HCRF 0x20u /* host command register full: a byte waits */
#define AP_3C505_ASR_ACRE 0x40u /* adapter command register empty: send another */
#define AP_3C505_ASR_ARDY 0x80u /* data register ready, in the current direction */

/* The flags `[DEV]` §1.9 named, kept as an enumeration because it is the one
 * place the eleven appear together and because two of them (`ACRE`/`HCRE`,
 * `ACRF`/`HCRF`) are easy to mistake for one another. The masks above are what
 * a model uses; this is what a reader reads. */
typedef enum {
  /* Command register handshake, `[DEV]` §1.9.1. */
  AP_3C505_FLAG_ACRE, /* adapter command register empty, in `ASR` */
  AP_3C505_FLAG_ACRF, /* adapter command register full, in `HSR` */
  AP_3C505_FLAG_HCRE, /* host command register empty, in `HSR` */
  AP_3C505_FLAG_HCRF, /* host command register full, in `ASR` */
  /* Data register, §1.9.2. */
  AP_3C505_FLAG_ARDY, /* adapter data register ready, in `ASR` */
  AP_3C505_FLAG_HRDY, /* host data register ready, in `HSR` */
  AP_3C505_FLAG_DIR,  /* transfer direction, in `HCR`, `HSR` and `ASR` */
  /* General purpose, §1.9.5, and *not decoded by the hardware*. */
  AP_3C505_FLAG_ASF1,
  AP_3C505_FLAG_ASF2,
  AP_3C505_FLAG_ASF3,
  AP_3C505_FLAG_HSF1,
  AP_3C505_FLAG_HSF2,

  AP_3C505_FLAG_COUNT
} ap_3c505_flag_t;

/* ## The command set, `[DEV]` §3.1 and Table 1
 *
 * The firmware "idles waiting for a Primary Command Block (PCB) from the PC
 * Host". Host commands occupy `00`-`2f` and adapter responses `30`-`5f`, and
 * the table's own shape is the interesting part: **a response code is its
 * command code plus `0x30`**, uniformly, from `01`/`31` to `11`/`41`.
 *
 * The two entries that break it are the ones that confirm it. `04`/`05` are
 * the **DMA** download and upload, and their responses `34`/`35` are named
 * "download data request" and "upload data request" -- the adapter asking the
 * host to run the DMA cycle. `06`/`07` are the **PIO** forms of the same two
 * transfers, and Table 1 marks `36`/`37` `n/a`: nothing has to be requested
 * back, because the host is doing the moving itself. So the hole in the
 * response space is a statement about who drives the transfer, not a gap.
 *
 * Codes `12`-`2f` and `42`-`5f` are reserved. Modelling them as *reserved*
 * rather than as invalid matters: an adapter that rejects them is a guess, and
 * `[DEV]` does not say what this ROM does with one.
 */
typedef enum {
  AP_3C505_CMD_CONFIGURE_ADAPTER_MEMORY = 0x01, /* set buffer requirements */
  AP_3C505_CMD_CONFIGURE_82586 = 0x02,          /* set receive mode */
  AP_3C505_CMD_GET_ETHERNET_ADDRESS = 0x03,
  AP_3C505_CMD_DOWNLOAD_DATA_DMA = 0x04,
  AP_3C505_CMD_UPLOAD_DATA_DMA = 0x05,
  AP_3C505_CMD_DOWNLOAD_DATA_PIO = 0x06,
  AP_3C505_CMD_UPLOAD_DATA_PIO = 0x07,
  AP_3C505_CMD_RECEIVE_PACKET = 0x08,
  AP_3C505_CMD_TRANSMIT_PACKET = 0x09,
  AP_3C505_CMD_NETWORK_STATISTICS = 0x0Au, /* includes 82586 error counts */
  AP_3C505_CMD_LOAD_MULTICAST_LIST = 0x0Bu,
  AP_3C505_CMD_CLEAR_DOWNLOADED_PROGRAMS = 0x0Cu,
  AP_3C505_CMD_DOWNLOAD_PROGRAM = 0x0Du,
  AP_3C505_CMD_EXECUTE_PROGRAM = 0x0Eu,
  AP_3C505_CMD_SELF_TEST = 0x0Fu,
  AP_3C505_CMD_SET_ETHERNET_ADDRESS = 0x10u,
  AP_3C505_CMD_ADAPTER_INFO = 0x11u,

  AP_3C505_CMD_FIRST = 0x01u,
  AP_3C505_CMD_LAST = 0x11u,     /* `12`-`2f` are reserved */
  AP_3C505_CMD_RESERVED_END = 0x2Fu
} ap_3c505_command_t;

/* What separates a response from the command it answers. */
#define AP_3C505_RESPONSE_BIAS 0x30u

/* Whether a code is a command this ROM version implements. Reserved codes
 * answer `false`: they are in the host's half of the space but name nothing. */
[[nodiscard]] bool ap_3c505_command_is_implemented(uint8_t code);

/* The response code for a command, or `false` if the command has none. The
 * out-parameter shape is not decoration -- `06` and `07` are implemented
 * commands *without* a response code, so "no response" has to be sayable
 * without picking a sentinel value that is itself a valid code. */
[[nodiscard]] bool ap_3c505_response_for(uint8_t command, uint8_t *response);

/* Whether an address falls in a card's sixteen I/O locations, and where. */
[[nodiscard]] bool ap_3c505_decode(uint32_t base, uint32_t address,
                                   uint32_t *offset);

/* ## The mailbox, which is the whole of what a host-side model owes
 *
 * The board is an 80186 with an 82586 beside it, so nothing the host writes is
 * *executed* here: the host puts bytes in a command register and a data FIFO,
 * the adapter takes them out, and four flag registers say whose turn it is.
 * `[DEV]` §1.9 is that protocol and it is all this structure holds.
 *
 * **Two registers are stored and two are derived, and that is the design.**
 * `HCR` and `ACR` are written -- one by each side -- and `HSR` and `ASR` are
 * assembled on each read out of the other side's control register plus the
 * mailbox's own state. Storing all four would let them disagree, and the pairs
 * that would then drift are exactly the ones §1.9 warns are easy to confuse:
 * `HCRE`/`ACRE` and `HCRF`/`ACRF` are one byte's occupancy seen from two sides,
 * not two facts.
 *
 * The adapter side is modelled as a peer rather than as firmware: this holds
 * what the adapter has written and what it has taken, and something else -- the
 * PCB protocol host-side, or an emulated 80186 -- decides *when*. That decision
 * is still open and this structure does not prejudge it, which is the reason
 * the interface was built before it.
 */
typedef struct {
  uint8_t hcr;     /* Host Control Register, written by the host at `+6` */
  uint8_t acr;     /* Adapter Control Register, written by the adapter */
  uint8_t aux_dma; /* Host Aux DMA Register, `+2` on a write */

  /* The command register is **one byte in each direction**, not a queue.
   * §1.9.1's handshake is "full" and "empty" for a single byte, and a model
   * with a queue would report `HCRE` set while a byte was still in flight. */
  uint8_t to_adapter;
  bool to_adapter_full;
  uint8_t to_host;
  bool to_host_full;

  /* "A half duplex 20 byte FIFO": one buffer, and `DIR` says which way it is
   * pointing. Half duplex is why there is not one each way. */
  uint8_t fifo[AP_3C505_DATA_FIFO];
  unsigned fifo_count;

  bool dma_done;     /* terminal count reached, reported as `HSR`'s `DONE` */
  bool test_jumper;  /* the TEST jumper, reported as `ASR`'s `SWTC` */
  bool sixteen_bit;  /* a 16-bit slot, reported as `ASR`'s `8_16` */

  /* Demand mode's two pieces of state, both `[DEV]` §1.9.4's third deactivating
   * condition and neither of them a transfer's business: how many cycles have
   * run since the last pause, and whether the pause itself is owed. */
  unsigned dma_since_pause;
  bool dma_pause_owed;

  /* **The adapter's power-on, which its own option ROM is the specification
   * for.** `3000_3C505_010728-00`'s `entry_05` self-test hard-resets the card
   * with `HCR = $C0` then `HCR = $00`, and then polls `HSR`'s low two bits --
   * `ASF1` and `ASF2` -- first until they read **11** and then until they read
   * **00**, each with its own timeout and its own failure exit. So a healthy
   * board raises both adapter status flags while its 80186 is initialising and
   * drops them when it is ready.
   *
   * `[DEV]` §1.9.5 says the hardware does not decode these flags "in any way",
   * which is why this lives here as *adapter* state rather than as a register
   * rule: it is the firmware's convention, and the firmware that talks to the
   * real card is the source. Exactly the ring ROM's role in `RING.md` 60-68. */
  bool adapter_initialising;
} ap_3c505_t;

/* Power-on state. Also what a hard reset produces, so the two cannot drift. */
void ap_3c505_reset(ap_3c505_t *card);

/* The host side of the sixteen I/O locations. `offset` is what
 * `ap_3c505_decode` returned. A read of a write-only location answers `FF`,
 * which is what an undriven AT bus does and what this machine's board already
 * models for an empty slot. */
[[nodiscard]] uint8_t ap_3c505_read(ap_3c505_t *card, unsigned offset);
void ap_3c505_write(ap_3c505_t *card, unsigned offset, uint8_t value);

/* The two status registers, assembled rather than stored. Exposed because the
 * adapter half of the protocol reads `ASR` exactly as the host reads `HSR`, and
 * because a test that wants a flag should not have to go through a bus read. */
[[nodiscard]] uint8_t ap_3c505_host_status(const ap_3c505_t *card);
[[nodiscard]] uint8_t ap_3c505_adapter_status(const ap_3c505_t *card);

/* The adapter's side of the mailbox: what the 80186 would do. Kept here so the
 * host side has something to talk to before the decision about how the adapter
 * is driven has been made. */
[[nodiscard]] bool ap_3c505_adapter_take_command(ap_3c505_t *card,
                                                 uint8_t *command);
void ap_3c505_adapter_post_command(ap_3c505_t *card, uint8_t response);
void ap_3c505_adapter_write_control(ap_3c505_t *card, uint8_t value);

/* Whether the card is asserting its interrupt line. §1.10: the host is
 * interrupted when a command byte arrives with `CMDE` set, or when the DMA
 * reaches terminal count with `TCEN` set. */
[[nodiscard]] bool ap_3c505_irq(const ap_3c505_t *card);

/* ## The host DMA channel, `[DEV]` §1.9.4
 *
 * The card moves packet data through the same Data Register either way: the
 * host may run it by polled I/O or hand it to a DMA controller, and §1.9.4 is
 * explicit that "the Adapter and the Host may perform DMA transfers independent
 * of one another". So these are not a second data path -- they are the FIFO
 * again, with the demand-mode accounting the PIO side has no reason to keep.
 *
 * §1.9.4's truth table, read from the page image because the text layer renders
 * it as broken pipes and stray digits:
 *
 *     TRANSFER   | DIR | HRDY | ARDY | DESCRIPTION
 *     DMA        |     |  1   |  X   | WRITE REQUEST TO HOST
 *     DOWNLOAD   |  0  |  X   |  1   | READ REQUEST TO ADAPTER
 *     DMA        |     |  X   |  1   | WRITE REQUEST TO ADAPTER
 *     UPLOAD     |  1  |  1   |  X   | READ REQUEST TO HOST
 *
 * The two `HRDY` rows are the host's and the two `ARDY` rows the adapter's, so
 * **the host-side request is `HRDY` in both directions** and `DIR` selects only
 * whether the cycle reads or writes. A model that gated the host's line on
 * `ARDY` would have taken the adapter's half of the same table. */

/* Whether the card is driving its `DRQ` line.
 *
 * §1.9.4: "if the DMAE bit is set, the DMA request input to the host PC will go
 * inactive under the following conditions: 1. The entire Host DMA transfer is
 * completed 2. The Data Register FIFO is temporarily full/empty depending on
 * the transfer direction. 3. The Burst bit is not set and 9 DMA transfers have
 * occured since the last DMA pause." Those are `dma_done`, `HRDY` and the
 * demand-mode counter respectively, and all three are checked here.
 *
 * **Not `const`, and that is the pause.** Condition 3 relinquishes the channel
 * "for one host CPU cycle", so exactly one sample must read inactive before the
 * line comes back. The board samples this once per bus tick, which is that
 * cycle; consuming it here is what makes the pause one tick long rather than
 * permanent. */
[[nodiscard]] bool ap_3c505_dma_request(ap_3c505_t *card);

/* One DMA cycle's byte, each direction, counted toward the demand-mode pause.
 * `read` is the adapter-to-host direction the host's controller drives on an
 * upload; `write` is the download. Both go through the FIFO exactly as the
 * Data Register does, because they are the same register. */
[[nodiscard]] uint8_t ap_3c505_dma_read(ap_3c505_t *card);
void ap_3c505_dma_write(ap_3c505_t *card, uint8_t value);

/* Terminal count from the *host's* DMA controller, which is the only thing that
 * knows the transfer is over -- the card counts bytes through a FIFO and has no
 * length. `[HIS]` p. 3-4: "The DONE flag is set when a DMA transfer between the
 * host and the Data Register is complete. An interrupt to the host will also be
 * generated if the TCEN bit in the Host Control Register is set." */
void ap_3c505_dma_terminal_count(ap_3c505_t *card);

/* ## The Primary Command Block, `[DEV]` §3.1
 *
 * "The 3C505 firmware idles waiting for a Primary Command Block (PCB) from the
 * PC Host", and the format is three fields:
 *
 *     PCB command code   (byte)
 *     PCB data length    (byte)
 *     PCB data           (variable length)
 *
 * **"The PCB is passed using programmed I/O through the Command Register."**
 * That sentence is why this layer sits on the command register and not on the
 * data FIFO: the FIFO is §1.9.2's bulk path for packet contents, while a PCB --
 * 64 bytes at the most -- crosses one byte at a time through the same register
 * a bare command byte uses, under the same `ACRF`/`HCRE` handshake. A model
 * that framed PCBs over the FIFO would work until a driver interleaved the two.
 *
 * ## Host-side, by decision
 *
 * The adapter is an 80186 running firmware, and this project models the *host
 * side of the protocol* rather than emulating that processor: Domain/OS only
 * ever sees the mailbox, the command set is transcribed in full with its
 * response rule tested, and an 80186 plus an 82586 is a great deal of machinery
 * to reach the same observable bytes. The cost is stated rather than hidden --
 * anything the firmware does *beyond* the documented command set is invisible
 * here, and an oracle diff is what would find it.
 *
 * The framing below is deliberately separate from command dispatch. §3.1 is the
 * envelope and §3.2 is what is inside it; the envelope is the same for every
 * command, so it is built and tested once.
 */
#define AP_3C505_PCB_LENGTH_MAX AP_3C505_PCB_DATA_MAX

typedef struct {
  uint8_t command;
  uint8_t length;
  uint8_t data[AP_3C505_PCB_DATA_MAX];
} ap_3c505_pcb_t;

/* ## The two general-purpose flags carry the transfer protocol, §3.1.1
 *
 * §1.9.5 says the *hardware* does not decode `HSF1`/`HSF2` or `ASF1`-`ASF3` "in
 * any way", and the register model above duly passes them through. §3.1.1 is
 * what the *firmware* on either side agrees they mean, and it is a four-state
 * code in the low two:
 *
 *     SF2 SF1
 *      0   0    undefined
 *      0   1    PCB accepted
 *      1   0    PCB rejected
 *      1   1    end of PCB
 *
 * Which is exactly the separation this file keeps: the hardware layer must not
 * interpret them, and the layer replacing the firmware must. */
typedef enum {
  AP_3C505_SF_UNDEFINED = 0,
  AP_3C505_SF_ACCEPTED = 1,
  AP_3C505_SF_REJECTED = 2,
  AP_3C505_SF_END_OF_PCB = 3,
} ap_3c505_sf_t;

/* Each side's two flags, out of the control register it writes them in. */
[[nodiscard]] ap_3c505_sf_t ap_3c505_host_flags(const ap_3c505_t *card);
[[nodiscard]] ap_3c505_sf_t ap_3c505_adapter_flags(const ap_3c505_t *card);
void ap_3c505_set_host_flags(ap_3c505_t *card, ap_3c505_sf_t state);
void ap_3c505_set_adapter_flags(ap_3c505_t *card, ap_3c505_sf_t state);

/* ## Receiving a PCB, §3.1.1 -- and it is not framed by its length field
 *
 * "The Adapter uses a 64-byte circular buffer to store the host byte stream
 * sent through the Command Register. For protection against stray bytes (from
 * Host aborted PCB transfers), the Adapter does not consider a PCB transfer
 * complete until the Host Status Flags (HSF2 and HSF1) go to state 11.
 * Simultaneously, the TOTAL length of the PCB should be in the Command Register
 * so the true beginning of PCB can be calculated. (This last total length is
 * NOT included in the PCB data length field.)"
 *
 * So a receiver that counted the length field and declared the PCB complete --
 * the obvious reading of §3.1's three-field format, and what this file did
 * first -- gets the common case right and the case the design exists for
 * wrong. An aborted transfer leaves stray bytes in the buffer, and it is the
 * **trailing total length** that says where the real PCB started; the length
 * field inside it cannot, because it is one of the bytes in question.
 *
 * The buffer is 64 bytes because that is the largest PCB, so a stray prefix is
 * simply overwritten as the stream goes round. */
typedef struct {
  uint8_t buffer[AP_3C505_PCB_MAX];
  unsigned written; /* total bytes ever fed; the write position is modulo 64 */
} ap_3c505_pcb_rx_t;

void ap_3c505_pcb_rx_reset(ap_3c505_pcb_rx_t *rx);

/* Feed one byte from the command register. No completion test: §3.1.1's
 * completion is a flag transition, not a byte count. */
void ap_3c505_pcb_rx_byte(ap_3c505_pcb_rx_t *rx, uint8_t byte);

/* End of PCB: the last byte fed was the total length. Recovers the PCB that
 * total names, or returns false when the buffer cannot hold one -- a total
 * longer than the buffer, longer than the stream so far, or inconsistent with
 * the data-length field it turns out to contain. */
[[nodiscard]] bool ap_3c505_pcb_rx_end(const ap_3c505_pcb_rx_t *rx,
                                       ap_3c505_pcb_t *pcb);

/* Handing a PCB out a byte at a time. §3.1.3 is the mirror of §3.1.2: the
 * adapter sends code, length and data, then sets its flags to `11` and writes
 * the total length as one further byte. */
typedef struct {
  ap_3c505_pcb_t pcb;
  unsigned sent;
  bool active;
} ap_3c505_pcb_tx_t;

void ap_3c505_pcb_tx_start(ap_3c505_pcb_tx_t *tx, const ap_3c505_pcb_t *pcb);

/* The next byte, or false when the body is finished and the total length is
 * what should follow. `ap_3c505_pcb_total_length` is that byte. */
bool ap_3c505_pcb_tx_next(ap_3c505_pcb_tx_t *tx, uint8_t *byte);

/* "the TOTAL length of the PCB (excluding this byte)": the two header bytes
 * plus the data field. */
[[nodiscard]] uint8_t ap_3c505_pcb_total_length(const ap_3c505_pcb_t *pcb);


/* ## The wire, which the core does not own
 *
 * A frame has to go somewhere, and `src/core` knows nothing about sockets, TAP
 * devices or capture files -- a frontend supplies this. Keeping it a callback
 * is what lets the deterministic headless frontend hand over a replayable
 * capture while an interactive one hands over a real interface, without the
 * device model knowing which it has.
 *
 * `transmit` returns false when the frame could not be put on the wire, which
 * is a completion status the adapter reports rather than an error here. */
typedef struct {
  void *context;
  bool (*transmit)(void *context, const uint8_t *frame, unsigned length);
} ap_3c505_wire_t;

/* `[DEV]` §3.2.1 `02H`: the receive mode word. Bits 2-0 select what is
 * accepted and bits 4-3 the loopback mode, both defaulting to zero. */
#define AP_3C505_RX_STATION_ONLY 0u
#define AP_3C505_RX_PLUS_BROADCAST 1u
#define AP_3C505_RX_MULTICAST 2u
#define AP_3C505_RX_PROMISCUOUS 4u
#define AP_3C505_RX_MODE_MASK 0x07u
#define AP_3C505_LOOPBACK_NONE 0u
#define AP_3C505_LOOPBACK_INTERNAL 1u
#define AP_3C505_LOOPBACK_EXTERNAL 2u
#define AP_3C505_LOOPBACK_SHIFT 3u
#define AP_3C505_LOOPBACK_MASK 0x18u


/* The frame limit is Ethernet's, not the manual's: `[DEV]` gives no maximum
 * packet size, and 1514 is the Ethernet II maximum without the frame check
 * sequence. Named as that rather than presented as a figure from the guide. */
#define AP_3C505_FRAME_MAX 1514u

/* §3.2.1 `0BH`: "The maximum number of addresses in the PCB is ten." */
#define AP_3C505_MULTICAST_MAX 10u
#define AP_3C505_ADDRESS_BYTES 6u

/* The firmware this project replaces, as state. Everything here is something a
 * documented command sets or a documented response reports -- nothing is here
 * to make a model tidy. */
typedef struct {
  uint8_t address[AP_3C505_ADDRESS_BYTES]; /* the Ethernet address PROM's */
  uint16_t receive_mode;

  uint8_t multicast[AP_3C505_MULTICAST_MAX][AP_3C505_ADDRESS_BYTES];
  unsigned multicast_count;

  /* §3.2.2 `3AH`'s counters, in its order and its widths. */
  uint32_t receive_packets;
  uint32_t transmit_packets;
  uint16_t crc_errors;
  uint16_t alignment_errors;
  uint16_t no_resource_errors;
  uint16_t overrun_errors;

  ap_3c505_wire_t wire;

  /* The PCB the adapter is handing to the host, if any. §3.1.3 sends it a byte
   * at a time through the command register as the host empties it, so it has to
   * be held across many bus cycles rather than written in one go. */
  ap_3c505_pcb_tx_t pending;
  bool pending_total; /* the body is done; the total length is the next byte */

  /* `09H`'s armed transmit: how many bytes the host said it would download, and
   * what has arrived so far. */
  bool transmitting;
  unsigned transmit_length;
  unsigned transmit_received;
  uint16_t transmit_offset;
  uint16_t transmit_segment;
  uint8_t transmit_buffer[AP_3C505_FRAME_MAX];

  /* `08H`'s armed receive, and the frame staged for upload once one arrives. */
  bool receive_armed;
  uint16_t receive_offset;
  uint16_t receive_segment;
  uint16_t receive_buffer_length;
  uint16_t receive_timeout;
  unsigned staged_length;
  unsigned staged_read;
  uint8_t staged[AP_3C505_FRAME_MAX];
} ap_3c505_adapter_t;

void ap_3c505_adapter_init(ap_3c505_adapter_t *adapter,
                           const uint8_t address[AP_3C505_ADDRESS_BYTES]);

/* ## The data phase, `[DEV]` §3.2.1 `08H` and `09H`
 *
 * These two are not single-PCB commands and that is the whole of their
 * difficulty. `09H`: "If the PCB is accepted, the Host should DMA download the
 * packet data through the Data Register. When the transmit is complete, the
 * Adapter responds with PCB 39H." `08H`: "When the receive is complete, the
 * Adapter responds with PCB 38H ... The Adapter will DMA upload the packet
 * through the Data Register."
 *
 * So each is: a PCB that arms an operation, a bulk transfer over the FIFO in
 * the direction `DIR` names, and a response PCB at the end. `ap_3c505_dispatch`
 * answers nothing for either -- the response is produced by the functions
 * below, when the data has actually moved. */

/* Queue a PCB for the host. §3.1.3's sequence -- code, length, data, then the
 * flags to `11` and the total length -- is produced by `ap_3c505_pump` as the
 * host takes each byte. */
void ap_3c505_adapter_post_pcb(ap_3c505_adapter_t *adapter,
                               const ap_3c505_pcb_t *pcb);

/* Move one byte of any queued PCB into the command register, if the host has
 * emptied it. Returns true when a byte was placed. This is what turns a queued
 * response into the byte stream §3.1.3 describes, and it is the *card* that
 * paces it -- the host reading is what makes room. */
bool ap_3c505_pump(ap_3c505_t *card, ap_3c505_adapter_t *adapter);

/* `09H` armed the transmitter; feed it the bytes the host downloads. Returns
 * true when the last byte has arrived, at which point the frame has been handed
 * to the wire and `out` is the `39H` completion response. */
bool ap_3c505_transmit_byte(ap_3c505_adapter_t *adapter, uint8_t byte,
                            ap_3c505_pcb_t *out);

/* A frame arriving from the wire. Returns true when a receive was armed and the
 * frame was accepted, at which point `out` is the `38H` response and the frame
 * is staged for the host to read through the data register.
 *
 * "The number of bytes DMA'ed will not exceed the buffer length specified in
 * the receive packet command PCB 8H; extra packet data is discarded" -- so a
 * frame longer than the host's buffer is truncated rather than refused, and
 * `38H` reports the two lengths separately so the host can tell. */
bool ap_3c505_deliver_frame(ap_3c505_adapter_t *adapter, const uint8_t *frame,
                            unsigned length, ap_3c505_pcb_t *out);

/* The staged received frame, a byte at a time, as the host reads the data
 * register. False when there is nothing left. */
bool ap_3c505_receive_byte(ap_3c505_adapter_t *adapter, uint8_t *byte);

/* Execute one request PCB. Returns true when a response PCB is produced --
 * `04`-`07` are the transfers that have none, and Table 1's two `n/a` response
 * codes are the same fact seen from the other side.
 *
 * Commands whose response format this project has not read are **refused**
 * rather than answered with invented contents; §3.1.1's `10` (rejected) is the
 * protocol's own way to say so. */
bool ap_3c505_dispatch(ap_3c505_adapter_t *adapter, const ap_3c505_pcb_t *in,
                       ap_3c505_pcb_t *out);


/* `09H` armed the transmitter; feed it the bytes the host downloads. Returns
 * true when the last byte has arrived, at which point the frame has been handed
 * to the wire and `out` is the `39H` completion response. */
bool ap_3c505_transmit_byte(ap_3c505_adapter_t *adapter, uint8_t byte,
                            ap_3c505_pcb_t *out);

/* A frame arriving from the wire. Returns true when a receive was armed and the
 * frame was accepted, at which point `out` is the `38H` response and the frame
 * is staged for the host to read through the data register.
 *
 * "The number of bytes DMA'ed will not exceed the buffer length specified in
 * the receive packet command PCB 8H; extra packet data is discarded" -- so a
 * frame longer than the host's buffer is truncated rather than refused, and
 * `38H` reports the two lengths separately so the host can tell. */
bool ap_3c505_deliver_frame(ap_3c505_adapter_t *adapter, const uint8_t *frame,
                            unsigned length, ap_3c505_pcb_t *out);

/* The staged received frame, a byte at a time, as the host reads the data
 * register. False when there is nothing left. */
bool ap_3c505_receive_byte(ap_3c505_adapter_t *adapter, uint8_t *byte);

/* Execute one request PCB. Returns true when a response PCB is produced --
 * `04`-`07` are the transfers that have none, and Table 1's two `n/a` response
 * codes are the same fact seen from the other side.
 *
 * Commands whose response format this project has not read are **refused**
 * rather than answered with invented contents; §3.1.1's `10` (rejected) is the
 * protocol's own way to say so. */
bool ap_3c505_dispatch(ap_3c505_adapter_t *adapter, const ap_3c505_pcb_t *in,
                       ap_3c505_pcb_t *out);

#endif /* APOLLO_DEVICE_AP_3C505_H */
