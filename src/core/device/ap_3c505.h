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
 * ## The map, `[DEV]` §1.3.3
 *
 *     +0  Host Command Register      full duplex, byte wide
 *     +2  read:  Host Status Register
 *     +2  write: Host Control Register
 *     +4  Data Register              20-byte half duplex FIFO
 *     +6  read:  Host Control Register
 *
 * Two shapes in that table catch a reader out, and both are the manual's:
 * `+2` is **two different registers by direction**, and the control register is
 * **readable at a different offset than it is written**.
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

/* Offsets from the base. `+2` names two registers, by direction. */
#define AP_3C505_REG_COMMAND 0u        /* read and write */
#define AP_3C505_REG_STATUS 2u         /* read */
#define AP_3C505_REG_CONTROL_WRITE 2u  /* write */
#define AP_3C505_REG_DATA 4u           /* read and write */
#define AP_3C505_REG_CONTROL_READ 6u   /* read */

/* `[DEV]` §1.9.2: "a half duplex 20 byte FIFO". */
#define AP_3C505_DATA_FIFO 20u

/* `[DEV]` §3.1: the Primary Command Block the adapter idles waiting for --
 * a command byte, a length byte, then data. "The maximum PCB size the Adapter
 * can accept in this version ROM is 64 bytes", and the length "does not include
 * the PCB command code or the length field itself", so the data field is at
 * most 62. */
#define AP_3C505_PCB_MAX 64u
#define AP_3C505_PCB_DATA_MAX 62u

/* The named flags. **Positions are not known** -- `[DEV]` §1.9 defers them to a
 * document this project does not hold -- so these are an enumeration of what
 * exists, not a bit layout, and nothing here assigns them numbers. Naming them
 * without positions is the honest state: it records what the interface has
 * while making it impossible to use a position nobody has established. */
typedef enum {
  /* Command register handshake, `[DEV]` §1.9.1. */
  AP_3C505_FLAG_ACRE, /* adapter command register empty */
  AP_3C505_FLAG_ACRF, /* adapter command register full */
  AP_3C505_FLAG_HCRE, /* host command register empty */
  AP_3C505_FLAG_HCRF, /* host command register full */
  /* Data register, §1.9.2. */
  AP_3C505_FLAG_ARDY, /* adapter data register ready */
  AP_3C505_FLAG_HRDY, /* host data register ready */
  AP_3C505_FLAG_DIR,  /* transfer direction: clear host->adapter, set adapter->host */
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

#endif /* APOLLO_DEVICE_AP_3C505_H */
