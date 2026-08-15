# 3Com EtherLink Plus (3C505) — findings

The other network path. The Apollo Token Ring is this machine's own
(`RING.md`); the 3C505 is an ordinary AT expansion card that Domain/OS can also
use, and it is the **one networking path with a runnable reference** — MAME's
Apollo driver fits one by default and carries Domain networking over it.

That makes this the opposite situation from the ring's in every respect. There
are two manuals, there is an oracle, and the verification is an oracle diff
rather than a firmware self-test. The bit-level register description that
`[DEV]` deferred to another document was the one gap; `[HIS]` closes it —
finding 11.

Status legend: `confirmed` · `open` · `provisional` · `deliberate divergence`

## Sources

| Key | Document |
| --- | --- |
| `[DEV]` | *EtherLink Plus Developer's Guide*, 3Com, May 1986, 77 pp |
| `[HIS]` | *EtherLink Plus Technical Reference*, 3Com 1569-03, Jan 1989, 84 pp — the document `[DEV]` §1.9 defers to. **Found 2026-08-12** |
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
| 3 | ~~The map itself: `+2` write **Host Control Register**, `+6` read **Host Control Register**~~ — **WRONG, and it was in the header and its test.** See 3b | `[DEV]` §1.3.3 | retracted |
| 3b | **The map itself, offsets from the base:** `+0` **Host Command Register** (read and write); `+2` read **Host Status Register**, write **Host Aux DMA Register**; `+4` **Data Register**; `+6` **Host Control Register**, write, and readable only on Rev 3 hardware. `+2` is two different registers by direction; the control register is *not* one of them. `[DEV]` §1.3.3 says otherwise and **contradicts `[DEV]`'s own §2.1 register summary** (Control at host `6`, AUX DMA at host `2`, write only) and its §2.5, titled *Host Aux DMA Register*. `[HIS]` prints the summary twice — §2-3 as an address list, §3-1 as an offset table — and agrees with §2.1 both times, so three tables to one. A model following §1.3.3 writes the host's control word into the DMA burst register | `[HIS]` §2-3, §3-1; `[DEV]` §2.1, §2.5 | confirmed |
| 3a | **The Data Register's width is a property of the slot, not the card**: "byte wide register in an 8 bit slot (PC, XT, or AT) and word wide in a 16 bit slot (AT)". So the width this core presents is a *board* decision — the AT window's `MEM_CS16.L`/`IO_CS16.L` question `ap_atbus` already models — and not a device constant | `[DEV]` §1.3.3 footnote | confirmed |
| 4 | **The Command Register is full duplex and byte wide**, carrying commands and small amounts of data both ways. It can be polled through the Command Register Empty (`ACRE`, `HCRE`) and Command Register Full (`ACRF`, `HCRF`) bits in the two status registers, **or** driven by interrupt — an interrupt is raised to whichever side did not load the byte | `[DEV]` §1.9.1 | confirmed |
| 5 | **The Data Register is a half duplex 20-byte FIFO** for bulk transfer. Direction is set by the `DIR` bit in the **Host** Control Register: clear is host→adapter (a *download*), set is adapter→host (an *upload*), and `DIR` is readable in both status registers. Polled operation reads the Data Register Ready bits (`HRDY`, `ARDY`); DMA is the alternative | `[DEV]` §1.9.2 | confirmed |
| 6 | **The general-purpose status flags.** The adapter has three — `ASF1`, `ASF2`, `ASF3` — written through the Adapter Control Register and read by the host in the Host Status Register. The host has two, `HSF1` and `HSF2`, written through the Host Control Register and read by the adapter. "They are not decoded by the hardware in any way": they are firmware-to-driver convention, used for synchronisation and completion codes. **So a model of the hardware must pass them through and must not interpret them** | `[DEV]` §1.9.5 | confirmed |
| 7 | Adapter interrupts, the ones with numbers: **DMA Channel 1 Done** fires after the last cycle of a transfer to or from the Data Register, and **Timer 0 fires every 10 ms** for counting and timeouts. Both are internal to the 80186 and matter only if the adapter firmware is emulated rather than replaced | `[DEV]` §1.10.1 | confirmed |
| 8 | **The bit-level register description is in another document.** §1.9 says outright: "A detailed bit level description of these registers is found in the 3C505 Hardware Interface Specification, Chapter 2." `[DEV]` gives the registers, their widths, their directions and the *named* bits without giving their positions — a bounded gap, about ten bits in two byte-wide registers. **Closed by finding 11** | `[DEV]` §1.9 | closed |
| 8a | The plan was to close it from the oracle if the document could not be found, which the resolution order permits here — the document had been read first and had named what it does not contain. **It was closed by the document instead**, and the detour is recorded below because the oracle's numbers survived and its *framing* did not | — | closed |
| 9 | **The command set, all of it.** Host commands occupy `00`-`2f` and adapter responses `30`-`5f`. Implemented: `01` configure adapter memory, `02` configure 82586, `03` Ethernet address, `04`/`05` download/upload data by **DMA**, `06`/`07` download/upload data by **PIO**, `08` receive packet, `09` transmit packet, `0a` network statistics, `0b` load multicast list, `0c` clear downloaded programs, `0d` download program, `0e` execute program, `0f` self-test, `10` set Ethernet address, `11` adapter info. `00` names nothing and `12`-`2f` are reserved | `[DEV]` Table 1 | confirmed |
| 9a | **A response code is its command plus `0x30`**, uniformly across the table -- `01`/`31` through `11`/`41`. The two exceptions confirm the rule rather than breaking it: `04`/`05` are the DMA transfers and their responses `34`/`35` are named "download/upload data **request**", the adapter asking the host to run the cycle; `06`/`07` are the PIO forms of the same transfers and Table 1 marks `36`/`37` **`n/a`**, because the host moves the data itself and has nothing to be asked for. So the hole in the response space states who drives the transfer | `[DEV]` Table 1 | confirmed |
| 9b | Table 1 is a **two-column** table and the PDF's text layer interleaves the columns, pairing codes with the wrong descriptions. It was read from a 170 dpi page render instead. This is the `4(1/1/0)` -> `4(1/010)` trap in a different shape, and the same rule caught it | `[DEV]` p. 29, page image | confirmed |
| 10 | **The card's registers are where `[DEV]` and this board's AT decode jointly predict.** Tapping physical `058000`-`05800F` on the oracle shows every access landing on `058002` and `058006` — which through the AT window's *block* rule (`ap_disk.h`: base `0x040000 + AT x 0x80`, then "within each block the AT addresses run as consecutive bytes") are ISA `0x302` and `0x306`, i.e. the card's `+2` and `+6`. MAME's own translation, `isa_addr = (offset & 3) + ((offset & ~0x1ff) >> 7)` on word offsets, gives the same answer, and the two rules agree on the floppy's `3F2` as well. **So finding 2a's placement is confirmed by traffic, not just by arithmetic** | oracle write/read tap, 20 emulated seconds | confirmed |
| 10a | **The option ROM's probe handshake, measured.** All of it comes from PC `0008xxxx` — the card's own option ROM, not the boot PROM — and it is a four-step cycle repeated steadily: a **read-modify-write** at `+6` (the CPU's RMW shows as a read then a write at one address), then a read of `+2`; the RMW alternately clears and sets **bit 4** of `+6`, and `+2` reads `C0` while that bit is clear and `50` while it is set. So `+2`'s bit 7 follows the inverse of `+6`'s bit 4, its bit 4 follows it directly, and bit 6 is set throughout | oracle tap, PCs `080382`-`080398` | confirmed |
| 10b | ~~What 10a does not establish: which named flag each of those positions is~~ — closed by 10c | — | superseded |
| 11 | **`[HIS]` is found, and it gives all four flag registers in full** — `HCR` and `HSR` on the host side, `ACR` and `ASR` on the adapter's, eight named bits each, transcribed from the page images below. It also settles the host I/O map against `[DEV]` §1.3.3 (finding 3b) and states two things no mask table carries: `ATTN`+`FLSH` together are a hardware-decoded **hard reset** held while both bits are set, and `HCR` is **write-only on Rev 2 hardware**, readable only on Rev 3 | `[HIS]` §3-1 … §3-6, page images | confirmed |
| 10c | **`[HIS]` decodes 10a exactly, and 10a corroborates `[HIS]`.** The bit the option ROM toggles at `+6` is `HCR_DIR` (`10`), and the two status bytes it produces at `+2` are the Host Status Register: `C0` = `HRDY|HCRE` with `DIR` clear, `50` = `HCRE|DIR` with `HRDY` clear. Both are what an **empty FIFO** reads in the two directions — on a download `HRDY` set means "not full, send more"; on an upload `HRDY` clear means "empty, nothing to read" — and `HCRE` set throughout is a command register the host has not written. So the ROM is probing an idle card, every bit of both bytes is accounted for, and a layout recovered from a *document* explains traffic measured a day earlier from an *oracle*. Neither was derived from the other | `[HIS]` §3-3 + finding 10a | confirmed |

| 12 | **The card's place on this board is documented, in a chapter nothing here had cited: `[S3K]` chapter 14, "802.3 Network Controller-AT".** Seven pages of it, and §14.5's two figures give the whole strapping. **Figure 14-3, the standard configuration for an AT-compatible slot**, labels one jumper block "**DMA Channel 6 and Interrupt Level 10 Select**" with 6 and 10 fitted, and another "Control Status Registers Hex **Address 300**" — which through this board's `0x040000 + (ISA << 7)` is `058000`, finding 2a's placement from a third independent direction. So the two numbers this project was missing were on disk from the start, in the machine's own manual rather than the card's | `[S3K]` §14.5 Figure 14-3, page image | confirmed |
| 12a | **And Figure 14-4 is the second card**, the alternate configuration for an XT-compatible slot: I/O **`310`**, **DMA channel 3**, **interrupt level 9**. The pair matters beyond completeness because it **repairs a table the OCR destroys**. `[S3K]`'s DRQ table names controller "#2" on *both* DRQ3 and DRQ6, which cannot be right, and Table 2-3 gives IRQ9 to "#2" and IRQ10 to "#1". Figure 14-4 shows the alternate card is the one on DMA 3 and IRQ 9, so the standard card is #1 on DMA 6 / IRQ 10 and the mangled DRQ6 row is #1's. A reader who trusted the text layer would have wired the card to the wrong channel with a table apparently agreeing | `[S3K]` §14.5 Figure 14-4 + Table 2-3, page images | confirmed |
| 12b | Read from the **page images**, and this chapter is why the rule exists: its text layer renders neighbouring bus tables as `IRQ?`, `IRQQ`, `SA?` and `1/0`, and Figures 14-3 and 14-4 are *drawings* of jumper blocks whose extracted text carries no jumper positions at all. The numbers are legible only in the render | `[S3K]` pp. 14-3, 14-4 | confirmed |
| 13 | **The host DMA request line, from §1.9.4's truth table — and the host's half of it is `HRDY`, not `ARDY`.** The table has four rows: download (`DIR` 0) with `HRDY` 1 is a "write request to host" and with `ARDY` 1 a "read request to adapter"; upload (`DIR` 1) with `ARDY` 1 is a "write request to adapter" and with `HRDY` 1 a "read request to host". So the two `HRDY` rows are the host's line in both directions and `DIR` selects only whether the cycle reads or writes. The trap is real and inverted: on an empty FIFO in the upload direction `ARDY` is **set** while `HRDY` is **clear**, so a model that took the adapter's column would drive the host's channel exactly when the hardware does not | `[DEV]` §1.9.4, page image | confirmed |
| 13a | **And the three conditions that drop it, one of them with a number.** §1.9.4: "if the DMAE bit is set, the DMA request input to the host PC will go inactive under the following conditions: 1. The entire Host DMA transfer is completed 2. The Data Register FIFO is temporarily full/empty depending on the transfer direction. 3. The Burst bit is not set and 9 DMA transfers have occured since the last DMA pause." The pause is "one host CPU cycle" long, to let the PC refresh its DRAM. `[HIS]` p. 3-5 states the same rule in the same words three years later, so the nine is corroborated across both documents | `[DEV]` §1.9.4, `[HIS]` p. 3-5 | confirmed |
| 13b | **The Host Aux DMA Register is one bit wide.** `[HIS]` p. 3-5 prints seven cells reading `0` and one named `BRST`, so bit 0 is the whole register: "This register is cleared upon power-up. It doesn't exist on older Rev 2 hardware boards." That closes what `+2`-on-a-write actually holds, which finding 3b established the *existence* of without its contents | `[HIS]` p. 3-5, page image | confirmed |
| 13c | **`DONE` has exactly one way to clear, and it is not the obvious one.** `[HIS]` p. 3-4: "The DONE flag is set when a DMA transfer between the host and the Data Register is complete. An interrupt to the host will also be generated if the TCEN bit in the Host Control Register is set. **The DONE bit is cleared by clearing the DMAE bit in the Host Control Register.**" No status read clears it and no later transfer clears it — so a model that cleared it on either would drop a terminal count the host had not yet acknowledged. Ours did not clear it at all until this was read | `[HIS]` p. 3-4, page image | confirmed |

| 14 | **Measured, and the answer to question C is negative for this configuration: a full Domain/OS SR10.4 boot to `login:` with the card fitted never touches it.** The run's own region census -- which prints every region with a non-zero count and omits the rest -- has **no `ethernet` row at all**, against 14.9 M disk accesses and 34.2 M serial ones in the same run. The card was fitted: the report's header says `ethernet EtherLink Plus at 058000, no wire attached`, checked rather than inferred from the command line | `tools/identity-boot.sh --3c505`, 350 M instructions | confirmed |
| 14a | **And the state hash is `A354786119A3931D`, the reference boot's, unchanged.** That is the strongest form of the same result: fitting a card that is never addressed changes nothing, and it doubles as the regression check on finding 12's wiring -- `ap_3c505_irq` and `ap_3c505_dma_request` are sampled on every interrupt sample and every bus tick of the whole boot, with `ethernet_present` true, and the machine is bit-identical to the run before them | same run | confirmed |
| 14b | ~~Why it is not touched: the configuration table is uninitialised, so nothing records an Ethernet card as present~~ -- **CORRECTED by finding 15, one measurement later.** The boot does print `Configuration information is not initialized`, and `002398-04` p. 12-3's DEV BIT ARRAY bit 6 really is `ethernet`; both are true and **neither is the reason**. The reason is that the thing which drives this card is its own **option ROM**, and finding 14's run did not fit one. A causal claim built from two true facts and no second measurement, which is the trap this file has now fallen into five times | superseded by 15 | retracted |
| 15 | **With its option ROM fitted the card *is* driven -- 10,041 reads and 22 writes -- and the boot PROM's own test of it FAILS.** `--3c505-rom roms/firmware/3000_3C505_010728-00.bin` on the same 350 M boot gives an `EtherLink Plus` row in the census where finding 14 had none, and the console says exactly what happened: `network driver search started...` then **`802.3 Network Controller-AT test failed.`** and `Self test failed.` The run ends inside the boot PROM at `PC 000083C4` -- the PROM's *own* network test, not the option ROM at `080000` -- with serial reads at 82.7 M and one DUART register polled 23 M times, a firmware spin | `tools/identity-boot.sh --3c505-rom …`, 350 M instructions | confirmed |
| 15a | **Which is the good outcome, and it is the ring's situation one card along.** `CLAUDE.md` calls a controller's own firmware self-test "the hardware's test suite, for free", and this is the second one this project has got running. Correcting finding 15's own wording: the failing test is the **option ROM's** `entry_05`, not the boot PROM's -- the message is at `+45C` of `3000_3C505_010728-00` and the PROM's network driver search is merely its caller, which is why the stop lands in PROM code | finding 15, ROM string table | confirmed |
| 16 | **The 3c505 option ROM is the ring ROM's twin, and `tools/ring-rom/disasm.py` already reads it.** Same 0x4A-byte Apollo header, `rom_id` `'E   '`, checksum VALID, a five-record entry table with **`entry_05` at `+374` as the self-test** -- and the same five-message string table one word changed: `802.3: init error`, `transmit error`, `receive error`, `invalid controller no.`, `operation aborted`, against `RING.md` 51c's identical list for the ring. So the two boards ship the same firmware skeleton, and everything learned about `entry_05`'s shape on the ring applies here | `tools/ring-rom/disasm.py` over `3000_3C505_010728-00` | confirmed |
| 16a | **`entry_05` decoded end to end, and it is five steps.** (1) `$382`: ten times round, clear `HCR` bit 4 and require `HSR` bit 4 **clear**, then set it and require it **set** -- which is finding 10a's measured probe handshake seen from the firmware's side, the code that produced the traffic. (2) `$3AE`: claim the unit in a caller-supplied longword, failing if already claimed. (3) `$3C2`: **hard reset**, `HCR = $C0` then `HCR = $00`. (4) `$3CC`: poll until `(HSR & 3) == 3`, budget `50 x arg`. (5) `$3F0`: poll until `(HSR & 3) == 0`, budget `50000 x arg`. Both timeouts load **`$E08008F2`** and take the same failure exit; success prints "test passed." from `+414` | `3000_3C505_010728-00` `$374`-`$40A` | confirmed |
| 16b | **So the adapter's power-on is specified by its own firmware: both status flags up while it initialises, both down when ready.** `HSR` bits 1:0 are `ASF1`/`ASF2`, which `[DEV]` §1.9.5 says the hardware does not decode "in any way" -- they are firmware convention, and this ROM is the firmware. A board that never raised them fails poll (4); one that never dropped them fails poll (5). This is the same method `RING.md` 60-68 used on the ring's `+400`: the ROM asserts what a working board reads | finding 16a + `[DEV]` §1.9.5 | confirmed |
| 16c | **Two defects of ours, and the second was the larger.** The card never signalled a power-on at all, so poll (4) timed out. And `ap_3c505_pump` -- the adapter half's only driver -- was called by the frontend **only when a live TAP wire was attached**, so in every deterministic run the adapter side of the card never acted. A card whose adapter exists only when a host socket is open is not a card, and no amount of register work would have made the self-test pass while that held | measured | confirmed |
| 16d | **Fixed, and the firmware says so: `802.3 Network Controller-AT test passed.`** The same 350 M boot now runs the ROM's test to completion and continues into the SELF_TEST diagnostics on the normal path -- 35.4 M serial reads and 14.5 M disk against the reference boot's 34.2 M and 14.9 M, where the failing run had 82.7 M serial and one DUART register polled 23 M times. `EtherLink Plus` shows 322 reads and 22 writes: driven, then left alone | `tools/identity-boot.sh --3c505-rom …` | confirmed |
| 16e | And one existing test asserted `acr == 0` after a hard reset, which the firmware refutes: a healthy board comes out of reset **initialising**, with both flags up. That assertion encoded "nothing drives the adapter half" -- true of the model, not of the hardware -- and is the same shape as the ring suite's storage assumptions that `RING.md` 63a and 65a had to invert | `tests/etherlink_suite.c` | confirmed |
| 15b | **And it explains the design note the plan has carried since the mailbox was built.** "A card that answered but could not complete the test would fail it, which is worse than absent" was written as the reason the card is opt-in; this is that sentence measured. The default boot is unaffected -- finding 14's run returns the reference hash -- because the option ROM is opt-in too | `PROJECT_STATUS.md`, the opt-in rationale | confirmed |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | ~~Bit positions for the eleven named flags~~ | **CLOSED 2026-08-12.** `[HIS]` was found and gives all four registers in full; see the transcription below and findings 10c and 11. They are in `device/ap_3c505.h` with tests |
| C | Whether Domain/OS drives this card at all on a DN3500, or only recognises it | **Answered — findings 14, 15 and 15a, and the answer has two halves.** Without its option ROM the card is never addressed at all, in a whole 350 M boot to `login:`. **With** the ROM it is driven hard — 10,041 reads — by the boot PROM's network test, which then reports `802.3 Network Controller-AT test failed.` So the question is no longer "does anything drive it" but "what does its own test want", and that is a firmware-driven loop with a named stopping instruction (`000083C4`) rather than a documentary question. Domain/OS itself has still not been reached, because the PROM stops before loading it |

## Divergences from the oracle

None. The host-adapter mailbox is implemented (`device/ap_3c505.c`) and agrees
with the only oracle traffic captured for this card: finding 10a's probe cycle.
An idle card built from `[DEV]` and `[HIS]` alone answers `C0` with `DIR` clear
and `50` with `DIR` set, byte for byte, and `etherlink_suite` asserts it.

That is a check rather than a fit -- the model knows nothing of the measurement,
and the two bytes differ in three bits, so `HRDY`'s direction-dependent sense
(the easy thing to get backwards) would swap them.

## `[HIS]` IS FOUND: 1569-03, *EtherLink Plus Technical Reference*, Jan 1989

`3com/1569-03_EtherLink_Plus_Technical_Reference_Jan89.pdf`, 84 pages, from
bitsavers via the Internet Archive
(`bitsavers_3Com156903calReferenceJan89_4120018`).

**This is the document `[DEV]` §1.9 defers to** and which `device/ap_3c505.h`
recorded as not held. Its contents index alone settles what was missing: *Host
Status Register* at 3-3 and *Adapter Status Register* at 3-6, with `HCRE`,
`ACRF`, `HRDY` and `ARDY` named in the handshake prose -- "the host should
monitor the Host Status Register port for the HCRE bit ... before writing a byte
in the Command Register", and "poll the Host Status Register port for the ACRF
bit" for responses.

**So the oracle-sourced positions below are now second-best and should be
replaced by this document's**, per the resolution order: reference first, and
the oracle only when the documents genuinely run out. They had run out; they no
longer have. Read the register layouts from the **page images** at 3-3 and 3-6
rather than from a text extraction -- a bit table is exactly what OCR mangles,
and this project has already been bitten by that.

**A trap this download hit, worth recording**: the first fetch returned
3,571,244 bytes of an expected 4,120,018 and `pdftotext` failed with "Couldn't
find trailer dictionary". A truncated PDF is not obviously truncated -- `file`
still calls it a PDF. Check the byte count against the source's metadata, or
`pdfinfo` for a page count, before concluding a document lacks something.

## The four flag registers, from `[HIS]` §3-2, §3-3, §3-5 and §3-6

Read from the **page images**, per the rule that a bit table is exactly what a
text layer mangles. Every table in `[HIS]` is drawn most-significant bit
leftmost — §3-1 establishes it with `CMD7 … CMD0` — so the rows below run bit 7
down to bit 0 as printed.

**Host Control Register** — host `+6`, written by the host:

| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ATTN` | `FLSH` | `DMAE` | `DIR` | `TCEN` | `CMDE` | `HSF2` | `HSF1` |

**Host Status Register** — host `+2` on a read:

| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `HRDY` | `HCRE` | `ACRF` | `DIR` | `DONE` | `ASF3` | `ASF2` | `ASF1` |

**Adapter Control Register** — adapter `+3` write, `+2` read:

| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `LPBK` | `FLSH` | `R586` | `LED2` | `LED1` | `ASF3` | `ASF2` | `ASF1` |

**Adapter Status Register** — adapter `+3` on a read:

| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ARDY` | `ACRE` | `HCRF` | `DIR` | `8/16` | `SWTC` | `HSF2` | `HSF1` |

**The naming is the trap, and it is not arbitrary.** Each register is named
from the point of view of the side that *reads* it, so `HCRE` and `ACRF` live in
the **host**'s status register and `ACRE` and `HCRF` in the **adapter**'s. Each
side is being told the same two things about the one full-duplex command
register: whether the byte it sent has been taken, and whether one is waiting
for it.

The general-purpose flags cross at the **same bit**: `HSF1`/`HSF2` are written
by the host in `HCR` bits 0-1 and read by the adapter in `ASR` bits 0-1;
`ASF1`-`ASF3` are written by the adapter in `ACR` bits 0-2 and read by the host
in `HSR` bits 0-2. `[HIS]` §3-3 and §3-5 both say "routed directly". That is
§1.9.5's "not decoded by the hardware in any way" made concrete, and it is why
a model passes them through and interprets none of them.

Three further facts the layout carries, which a mask table alone would lose:

* **`ATTN` and `FLSH` set together are a hard reset**, decoded by the hardware
  as a pair: 80186, 82586, both status and both control registers, and the
  FIFO. The card stays in reset until *both* are cleared, so it is a level and
  not an edge. `ATTN` alone is a soft reset — an NMI to the 80186, leaving the
  registers alone. §3-2.
* **`HCR` is write-only on Rev 2 hardware** and readable only on Rev 3, the one
  with the large gate array (§3-1 footnote). Which revision the DN3500's card
  is has not been established, and it decides whether a driver can read its own
  control word back.
* **`8/16` reports the slot, not the card** (§3-6), which is the same shape as
  finding 3a: the Data Register's width is the board's decision, and this bit
  is where the adapter's firmware learns it.

### What this replaces

An earlier pass, before `[HIS]` was found, took these positions from
`ext/mame/src/devices/bus/isa/3c505.h` as facts about a register layout. **The
positions were right and the sides were swapped**: it labelled the `ARDY`/
`ACRE`/`HCRF` register "read by the host at `+2`" and the `HRDY`/`HCRE`/`ACRF`
one "the adapter's view", which is backwards in both cases. A host polling `+2`
for `ARDY` would have been reading `HRDY` and calling it the wrong name — the
same bit, the wrong story, and nothing would have failed until the two sides
disagreed about who was waiting.

That is the resolution order earning its keep twice over: the oracle's numbers
survived, the oracle's *framing* did not, and the document that settles it had
been named by `[DEV]` §1.9 all along.
