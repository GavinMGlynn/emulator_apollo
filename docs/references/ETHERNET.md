# 3Com EtherLink Plus (3C505) — findings

The other network path. The Apollo Token Ring is this machine's own
(`RING.md`); the 3C505 is an ordinary AT expansion card that Domain/OS can also
use, and it is the **one networking path with a runnable reference** — MAME's
Apollo driver fits one by default and carries Domain networking over it.

That makes this the opposite situation from the ring's in every respect. There
is a manual, there is an oracle, and the verification is an oracle diff rather
than a firmware self-test. What there is *not* is a bit-level register
description in the manual we hold — see finding 8.

Status legend: `confirmed` · `open` · `provisional` · `deliberate divergence`

## Sources

| Key | Document |
| --- | --- |
| `[DEV]` | *EtherLink Plus Developer's Guide*, 3Com, May 1986, 77 pp |
| `[HIS]` | *3C505 Hardware Interface Specification* — **not held**, named by `[DEV]` §1.9 as where the bit-level register description lives |
| `[S3K]` | *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 |
| `[ROM3C505]` | `roms/firmware/3000_3C505_010728-00` — the Apollo option ROM for this card |

`[DEV]` was re-fetched in August 2026 from the Internet Archive's bitsavers
mirror, item `bitsavers_3Com3c505EersGuideMay86_3677170`; `www.bitsavers.org/
pdf/3Com/` now 404s for it. **The copy previously on disk was half a file** —
1,851,086 bytes of 3,677,170 — which opens with a valid header and fails only
when read. Check the byte count before trusting a reference tree.

`[DEV]` structure, for citation: ch. 1 hardware external reference — 1.3 address
maps (1.3.1 adapter I/O, 1.3.2 adapter memory, 1.3.3 **host I/O**), 1.4 the
80186, 1.5 the 82586, 1.6 network interface, 1.7 firmware ROM, 1.8 adapter RAM,
1.9 **host-adapter interface** (1.9.1 command register, 1.9.2 data register,
1.9.3 its configuration, 1.9.4 DMA transfers, 1.9.5 status flags), 1.10 adapter
interrupts; ch. 3 host software interface.

## Established

| # | Finding | Source | Status |
| --- | --- | --- | --- |
| 1 | The card is an **intelligent adapter**, not a register-level Ethernet controller: an Intel **80186** running firmware from on-board ROM, with an Intel **82586** LAN coprocessor doing the wire work. The host never touches the 82586 — it talks to the 80186 through a mailbox. So what this core must model is the *host-adapter interface*, and the Ethernet behaviour behind it is the adapter firmware's | `[DEV]` §1.2, §1.4, §1.5 | confirmed |
| 2 | **The host I/O map, all five registers.** Sixteen I/O locations, base set by jumpers, **factory base `300H`** | `[DEV]` §1.3.3 | confirmed |
| 2a | Which, through this machine's AT decode `physical = 0x040000 + (ISA << 7)`, puts the card's base at physical **`058000`** — matching what `ap_board.h` already records for it and what MAME's driver uses. Two independent placements agreeing, from a manual rather than from the oracle | `[DEV]` §1.3.3 + `ap_board.h` | confirmed |
| 3 | The map itself, offsets from the base: `+0` **Host Command Register**; `+2` read **Host Status Register**, write **Host Control Register**; `+4` **Data Register**; `+6` read **Host Control Register**. Note `+2` is two different registers by direction, and the control register is readable at a *different* offset than it is written | `[DEV]` §1.3.3 | confirmed |
| 3a | **The Data Register's width is a property of the slot, not the card**: "byte wide register in an 8 bit slot (PC, XT, or AT) and word wide in a 16 bit slot (AT)". So the width this core presents is a *board* decision — the AT window's `MEM_CS16.L`/`IO_CS16.L` question `ap_atbus` already models — and not a device constant | `[DEV]` §1.3.3 footnote | confirmed |
| 4 | **The Command Register is full duplex and byte wide**, carrying commands and small amounts of data both ways. It can be polled through the Command Register Empty (`ACRE`, `HCRE`) and Command Register Full (`ACRF`, `HCRF`) bits in the two status registers, **or** driven by interrupt — an interrupt is raised to whichever side did not load the byte | `[DEV]` §1.9.1 | confirmed |
| 5 | **The Data Register is a half duplex 20-byte FIFO** for bulk transfer. Direction is set by the `DIR` bit in the **Host** Control Register: clear is host→adapter (a *download*), set is adapter→host (an *upload*), and `DIR` is readable in both status registers. Polled operation reads the Data Register Ready bits (`HRDY`, `ARDY`); DMA is the alternative | `[DEV]` §1.9.2 | confirmed |
| 6 | **The general-purpose status flags.** The adapter has three — `ASF1`, `ASF2`, `ASF3` — written through the Adapter Control Register and read by the host in the Host Status Register. The host has two, `HSF1` and `HSF2`, written through the Host Control Register and read by the adapter. "They are not decoded by the hardware in any way": they are firmware-to-driver convention, used for synchronisation and completion codes. **So a model of the hardware must pass them through and must not interpret them** | `[DEV]` §1.9.5 | confirmed |
| 7 | Adapter interrupts, the ones with numbers: **DMA Channel 1 Done** fires after the last cycle of a transfer to or from the Data Register, and **Timer 0 fires every 10 ms** for counting and timeouts. Both are internal to the 80186 and matter only if the adapter firmware is emulated rather than replaced | `[DEV]` §1.10.1 | confirmed |
| 8 | **The bit-level register description is in a document we do not have.** §1.9 says outright: "A detailed bit level description of these registers is found in the 3C505 Hardware Interface Specification, Chapter 2." `[DEV]` gives the registers, their widths, their directions and the *named* bits (`HCRE`, `HCRF`, `ACRE`, `ACRF`, `HRDY`, `ARDY`, `DIR`, `HSF1-2`, `ASF1-3`) without giving their positions. **This is the one gap, and it is bounded**: the names are known, so what is missing is an assignment of about ten bits to positions in two byte-wide registers | `[DEV]` §1.9 | open |
| 8a | And it is the gap the **oracle can close honestly**, unlike the ring's. This is the one networking path with a runnable reference, so bit positions can be recovered by driving MAME's `3c505.cpp` and reading what the Apollo option ROM and Domain/OS write — with the resolution order satisfied, because the document has been read first and named what it does not contain. Look for `[HIS]` on the web before measuring | — | open |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | Bit positions for the eleven named flags in the host status and control registers | `[HIS]` ch. 2 if it can be found; otherwise the oracle, per finding 8a |
| B | The command set the 80186 firmware implements — what a driver actually sends through the command register | `[DEV]` ch. 3 (host software interface), not yet read |
| C | Whether Domain/OS drives this card at all on a DN3500, or only recognises it | `[ROM3C505]`, and a booted machine |

## Divergences from the oracle

None yet: nothing is implemented.
