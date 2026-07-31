# Apollo Token Ring — findings

The ring has **no runnable reference implementation**: MAME's Apollo driver
carries Domain networking over an emulated 3c505 802.3 card instead. So every
fact here cites a primary source — a manual section, a patent, or an address in a
dumped firmware ROM. Nothing in this file is established by reasoning about our
own code.

Status legend: `confirmed` · `open` · `provisional` · `deliberate divergence`

## Sources

| Key | Document |
| --- | --- |
| `[MAC]` | *Apollo Token Ring Media Access Control Layer and Physical Layer Protocols*, order 010005-00 rev 00, Oct 1987 (29 pp) |
| `[S3K]` | *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 |
| `[EH]` | *Domain Engineering Handbook*, 002398-01/-03/-04 (Apr 83 / Feb 85 / Jan 87) |
| `[ARCH]` | *Apollo Domain Architecture*, Feb 1981 (and the Jan 1981 preliminary) |
| `[PAT575]` | US patent 4,716,575, *Adaptively synchronized ring network for a computer system* |
| `[AEGIS]` | *AEGIS Internals and Data Structures*, Jan 1986 |
| `[PLAN]` | *Planning Domain Networks and Internets*, 009916-A00, Aug 1988 |
| `[ROM3500]` | `roms/firmware/3500_RING_10666_6.bin` — 8 KB, rev 3.6 |
| `[ROM3000]` | `roms/firmware/3000_RING_1818-4882_9-4-90.bin` — 8 KB, rev 4.0 |
| `[ROM4500]` | `roms/firmware/4500_RING_10666_8.bin` — 8 KB |
| `[ROM5500]` | `roms/firmware/5500_RING_1818-4882_R9_12-10-90.bin` — 8 KB |

`[MAC]` structure, for citation: ch. 1 overview and protocol layers; ch. 2 MAC —
2.2.1.1 tokens free and claimed, 2.2.1.2 frame start and separator characters,
2.2.1.3 null separators, 2.2.2.1 frame start sequence, 2.2.2.2 packet header
sequence, 2.2.2.3 packet data sequence, 2.2.2.4 frame check sequence, 2.2.2.5
end-of-frame sequence; ch. 3 physical — 3.2 data stream, 3.3.1 phase-lock loops,
3.3.2 elastic-store buffer, 3.4 signal characteristics, 3.5 passive network
bypass; app. A coaxial driver, receiver and cable.

## Established

| # | Finding | Source | Status |
| --- | --- | --- | --- |
| 1 | Proprietary 12 Mbit/s ring over 75 Ω RG-6U coax, introduced 1981; not IEEE 802.5 and not interoperable with it | `[MAC]`, `[ARCH]` | confirmed |
| 2 | Controller occupies AT-bus I/O `0x220`–`0x23F`, appearing at Apollo physical `0x051000`; a second controller sits at AT `0x320`–`0x33F` / `0x059000` | `[S3K]` Table 2-9 | confirmed |
| 3 | Controller comprises a modem, input filtering and amplification, serial→parallel receive logic, parallel→serial transmit logic, a **dual-ported RAM buffer**, control logic, and relays that remove the node from the ring when powered off or offline | `[S3K]` §1.5.4 | confirmed |
| 4 | Older DN3000s use a two-board controller set; newer DN3000s and the DN4000 use a single board of the same function | `[S3K]` §1.5.4 | confirmed |
| 5 | Two board generations are in hand: Apollo part `10666` (DN3500, DN4500) and HP part `1818-4882` (DN3000, DN5500). Both ROMs identify as `Apollo Token Ring Network Controller-AT` | `[ROM3500]`, `[ROM4500]`, `[ROM3000]`, `[ROM5500]` | confirmed |
| 6 | Ring ROMs contain 68000 code and a self-test whose diagnostics are `ring: init error`, `ring: transmit error`, `ring: receive error` — so the firmware doubles as our first controller test | `[ROM3500]`, `[ROM3000]` string tables | confirmed |
| 6a | That 68000 code is executed by the *host* CPU rather than an on-board processor. **Inference, not yet confirmed:** `[S3K]` §1.5.4 enumerates the board's functional units and lists no microprocessor, and the ROM is 68000 rather than 8-bit code | `[S3K]` §1.5.4 + `[ROM3500]` | open — settle it in the disassembly |
| 7 | Both ROMs share an 8-byte prologue `33 5E 91 B6 00 00 A0 B6`, then ASCII `R` at +0x08 and a revision string at +0x14 (` 3.6` for `[ROM3500]`, ` 4.0` for `[ROM3000]`) — a common header format across both board generations | `[ROM3500]`, `[ROM3000]` | confirmed |
| 8 | Node ID PROM is a separate device at `0x011200`, distinct from the ring controller; 32 bytes (`3500_NI_1C874`). Domain/OS may instead take the node ID from the logical volume label of the first logical volume | `[S3K]` Table 2-8, MAME driver notes | confirmed |
| 9 | A single ring scaled to well over a hundred nodes without the degradation contemporary Ethernet suffered — the reason Apollo kept it proprietary | `[PLAN]` | confirmed |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | The controller's register map within `0x220`–`0x23F` | Disassemble `[ROM3500]` and `[ROM3000]` as 68000; every register recorded with the ROM address that proves it, and cross-checked across both board generations |
| B | Dual-ported RAM buffer: size, host window, and descriptor/queue layout | Same disassembly, plus `[S3K]` AT memory-space table |
| C | Exact token and frame character encodings on the wire | `[MAC]` §2.2.1–2.2.2, transcribed with each format citing its subsection |
| D | Ring latency contributed per node by the elastic-store buffer, and PLL acquisition behaviour | `[MAC]` §3.3, `[PAT575]`; a paper-oracle figure, since no runnable reference exists |
| E | Token-loss detection, ring reconfiguration and node insertion/removal timing | `[MAC]` ch. 2, `[PAT575]`, and Domain/OS driver behaviour observed under the MAME oracle's *host* side where applicable |
| F | Whether the 12 Mbit/s figure is the bit rate or the symbol rate — i.e. whether the PHY encoding puts the line clock above 12 MHz | `[MAC]` §3.2 and §3.4. **Bears directly on the time base**: a non-12 MHz line clock may not divide `AP_TIME_BASE_HZ`, in which case the base is recomputed |
| G | How Domain/OS's single-level store maps onto ring packets — page faults across nodes | `[AEGIS]`, `[ARCH]` |

Open item F is the one to resolve before the medium is implemented, because the
answer may change `AP_TIME_BASE_HZ`.

## Divergences from the oracle

None yet: there is no oracle for the ring to diverge from. When the 3c505 802.3
path is implemented, that one *does* have a runnable reference and gets its own
row in `tools/mame-oracle/FINDINGS.md`.
