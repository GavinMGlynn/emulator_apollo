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

/* Whether an address falls in a card's sixteen I/O locations, and where. */
[[nodiscard]] bool ap_3c505_decode(uint32_t base, uint32_t address,
                                   uint32_t *offset);

#endif /* APOLLO_DEVICE_AP_3C505_H */
