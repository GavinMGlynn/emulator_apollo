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
| 5a | `[ROM3000]` and `[ROM5500]` are **byte-identical** (md5 `6b8a2134474932c81acb3093c61619f4`), so the four dumps are three distinct images: `[ROM3500]`, `[ROM4500]`, and the one shared 1818-4882 image. The two dumps carry different dates in their filenames but the same content, so one image served the whole 1818-4882 generation. Disassemble it once, not twice | `[ROM3000]`, `[ROM5500]` | confirmed |
| 6 | Ring ROMs contain 68000 code and a self-test whose diagnostics are `ring: init error`, `ring: transmit error`, `ring: receive error` — so the firmware doubles as our first controller test | `[ROM3500]`, `[ROM3000]` string tables | confirmed |
| 6a | That 68000 code is executed by the *host* CPU rather than an on-board processor. **Inference, not yet confirmed:** `[S3K]` §1.5.4 enumerates the board's functional units and lists no microprocessor, and the ROM is 68000 rather than 8-bit code | `[S3K]` §1.5.4 + `[ROM3500]` | open — settle it in the disassembly |
| 7 | Both ROMs share an 8-byte prologue `33 5E 91 B6 00 00 A0 B6`, then ASCII `R` at +0x08 and a revision string at +0x14 (` 3.6` for `[ROM3500]`, ` 4.0` for `[ROM3000]`) — a common header format across both board generations | `[ROM3500]`, `[ROM3000]` | confirmed |
| 7a | Every option ROM in hand carries exactly **two non-zero bytes at offset `length`** — just past the checksummed image — followed by nothing but zero fill to the end of the 8 KB device: `$354F` `[ROM3500]`, `$BCF9` `[ROM4500]`, `$0057` `[ROM3000]`/`[ROM5500]`, `$56A0` on the 3C505 Ethernet ROM. Present across both board generations *and* across controller types, so it is part of the option-ROM format, not ring-specific. Purpose unknown — open question H | `[ROM3500]`, `[ROM4500]`, `[ROM3000]`, `[ROM5500]`, `3000_3C505_010728-00` | confirmed (existence), open (meaning) |
| 8 | Node ID PROM is a separate device at `0x011200`, distinct from the ring controller; 32 bytes (`3500_NI_1C874`). Domain/OS may instead take the node ID from the logical volume label of the first logical volume | `[S3K]` Table 2-8, MAME driver notes | confirmed |
| 9 | A single ring scaled to well over a hundred nodes without the degradation contemporary Ethernet suffered — the reason Apollo kept it proprietary | `[PLAN]` | confirmed |
| 10 | 12 Mbit/s is the **data** rate, not the symbol rate. The PHY is bi-phase encoded: "In the time it takes to transmit one bit (this is a bit cell, or 83.33 nsec), two windows exist: the clock window and the data window" — a clock window that must always carry a transition, and a data window whose transition (or absence) is the bit value. Each node encodes and decodes with "the 24-MHz clock generated by its transmit \[receive] phase-lock loop" | `[MAC]` §3.2 p.3-3 | confirmed |
| 10a | So the ring is **two** clock domains: a 12 MHz data clock and a 24 MHz line clock, and both must divide the time base exactly. This is what raised `AP_TIME_BASE_HZ` from 3.3 GHz to 6.6 GHz — 3.3 GHz divides 24 MHz only as 137.5 | `[MAC]` §3.2 p.3-3 | confirmed |
| 10b | 24 MHz is the transmit PLL's *nominal centre* frequency, not a fixed one: phase offset is ≤0.5 bit-times at 24 MHz −3 kHz and ≥1.5 bit-times at 24 MHz +3 kHz, and a node's elastic-store buffer re-initialises at 24 MHz after under/overflow. The emulated ring runs the nominal figure; per-node frequency deviation is a deliberate non-model until item D gives it a measured envelope | `[MAC]` §3.3.1–3.3.2 pp.3-4–3-5 | confirmed |

| 11 | The controller is addressed through a base pointer the firmware loads with `lea.l $59000.l, a2` (`[ROM3500]` `000CAA`, commented RING1 = AT I/O `0x320`–`0x33F`) and `lea.l $5a000.l, a2` (`000CBE`). That confirms finding 2's placement from the firmware's own side rather than from the manual's | `[ROM3500]` `000CAA`, `000CBE` | confirmed |
| 12 | **Four banks at `+000`, `+400`, `+800`, `+C00`, each with slots at `+0`, `+2`, `+4`, `+6`.** Fifteen distinct offsets are touched on the DN3500 and DN4500 ROMs and only the first bank's four on the DN3000's, so the bank structure is the *later* board's. Extracted mechanically from the listings, every offset with the ROM address of its first access | `[ROM3500]`, `[ROM4500]`, `[ROM3000]` disassembly | confirmed |
| 13 | `+400` carries a **status word whose bit 15 is polled**: `move.w $400(a2), d0` then `and.w #$8000, d0` then a branch, before the firmware clears `(a2)`, `+402`, `+404` and `+400` in that order. So `+400` is read for readiness and the group is then reset together | `[ROM3500]` `0000AE`–`0000C2` | confirmed |
| 14 | `+806` and `+C06` are **byte** registers written with the sequence `$30`, `$70`, `$B0` (`+806`) and `$30`, `$70` (`+C06`), immediately after that reset. Two banks driven with the same pattern one after the other suggests two identical sub-devices — consistent with finding 3's separate transmit and receive logic, though the assignment of which is which is *not* evidenced | `[ROM3500]` `0000C6`–`0000DE` | confirmed |
| 15 | `+000` is read with `movea.l (a2), a0` (`0008BC`, `0008FE`), so the first slot is read as a **long** and used as an address. `+800`, `+802`, `+804`, `+C00` and `+C02` appear only as `lea.l` operands — the firmware takes their addresses rather than their contents. Both point at those regions being **buffer** rather than register, which is question B's subject | `[ROM3500]` `0008BC`, `0008FE`, `000D62`–`000DA8` | confirmed |

| 16 | **The boot PROM's option-ROM validator, recovered.** `3500_BOOT_12191_7` matches a candidate in three variants at `01032`, `0104E` and `0106A`: all three require `magic0 = $335E91B6` at `+0`, then one requires `magic1 = $0000A0B6` at `+4` *and* a caller-supplied class in `d0` against `+8`, one requires `magic1` and a word match against `+1A`, and one requires `magic1 = $C000A0B7` alone. So `rom_id` at `+8` is matched against what the *caller* is looking for — `'R   '` when the firmware wants a ring board — which is why the same scan serves the ring and the 3C505 | `3500_BOOT_12191_7` `01032`, `0104E`, `0106A` | confirmed |
| 17 | **The checksum routine at `01080`, and what it does not read.** It skips the sum entirely when `hdr_ver` at `+18` is greater than 1, or when the fudge longword at `+10` is `$FFFFFFFF`; otherwise it takes `length` from `+0C`, shifts it right by two, and sums that many long words from the image base with `add.l (a0)+,d0`. The last long read is therefore at `length - 4`. The ring ROMs carry `hdr_ver = 1` and a real fudge longword, so they *are* checksummed | `3500_BOOT_12191_7` `01080`–`010AC` | confirmed |
| 17a | And the scan's shape was then seen from the running machine, which is the same code from the other side: a headless boot records the AT bus empty-slot addresses `00080000`-`00080003`, `00081000`-`00081003`, `00082000`-`00082003`, `00083000`-`00083003` — four bytes at each of four 4 KB slots, which is `magic0` being read and failing to match with no card fitted | measured, `--boot-limit 350000000` on the DN3500 | confirmed |
| 18 | **The four out-of-band characters, nine bits each.** Every one is a leading `0`, six `1`s, then two type bits, most-significant-bit first: `0 111111 00` separator, `0 111111 01` frame start, `0 111111 10` free token, `0 111111 11` claimed token — `0x0FC`, `0x0FD`, `0x0FE`, `0x0FF` as nine-bit values. They are *not* byte values; the leading zero occupies a bit time on the wire | `[MAC]` Figures 2-2 (p. 2-4), 2-3 and 2-4 (p. 2-5) | confirmed |
| 18a | **Read from the page images, not the text layer.** `pdftotext` renders those three figures as fragments like `MSB L.1_o--L_--'_ _ ..L~_U_S_T_BLiE_O_N_E......F~_-,--_---,-_1---11----,1 LSB` — a `1`, a `0`, and no way to tell which cell either belongs to. Rendered at 200 dpi with `pdftoppm` and read as images instead. `CLAUDE.md`'s rule about page images exists for exactly this | `[MAC]` pp. 2-4, 2-5 | confirmed |
| 19 | **Bit stuffing is what makes them recognisable.** "a transmitting node inserts a Zero ... after every five successive Ones", and the receiver extracts each zero following five ones — so six successive ones cannot occur in data, and their presence "tells a receiver: *the bit-stuffing protocol has been intentionally violated by the transmitter*". The two bits after the six ones then select the character | `[MAC]` §2.2.1 p. 2-4 | confirmed |
| 20 | Null separators: the **long** one is "a minimum of 8 bytes of Zeros" and precedes the packet; the **short** one is "a byte of Zeros" and occurs within the frame start sequence and within the frame check sequence. A minimum, modelled as a minimum | `[MAC]` §2.2.1.3 p. 2-5 | confirmed |
| 21 | **Packet header layout**: destination address `+0` (2 words), type `+4` (1 word), a zero byte `+6` and the early acknowledge byte `+7`, source address `+8` (2 words), header data from `+C`. 12 to 1024 bytes, always even — and 12 + 1012 = 1024, so the two figures close exactly. The controller "will always transmit the first 12 bytes ... even in a broken network", which is what beaconing rests on | `[MAC]` Figure 2-5 and §2.2.2.2 p. 2-6 | confirmed |
| 22 | **Type field** is a 16-bit word whose named bits are 7:1 — broadcast `<7>`, hardware diagnostics `<6>`, thank-you (reply) `<5>`, please (request) `<4>`, paging `<3>`, user/IPC `<2>`, software diagnostics `<1>`. Bits 15:8 **and bit 0** are reserved, so it is a seven-bit field and not an eight-bit one. Broadcast set means receivers ignore the destination field, whose bits are then "free for beaconing" | `[MAC]` Figure 2-6 p. 2-7 | confirmed |
| 23 | **Early acknowledge** (byte): bit 7 must be zero, 6:5 reserved, 4 must be zero, 3 intend-to-copy, 2 reserved, 1 odd parity, 0 must be zero. **Late acknowledge** (byte, in the end-of-frame sequence): 7 must be zero, 6 copied, 5 wait-ack, 4 must be zero, 3 intend-to-copy, 2 error, 1 odd parity, 0 must be zero. Both are inserted by the transmitter and modified by *other* nodes as the frame passes | `[MAC]` Figures 2-7 p. 2-8, 2-8 p. 2-9 | confirmed |
| 24 | **The CRC is not Ethernet's.** `[MAC]` gives the generator as a product, g(X) = (X^21 + 1)(X^11 + X^2 + 1), which expands to X^32 + X^23 + X^21 + X^11 + X^2 + 1 — register form `0x00A00805`, against Ethernet's `0x04C11DB7`. Initialised to zero, transmitted most-significant-bit first, no reflection or final inversion mentioned. A ring built on the familiar polynomial would emit frames no Apollo node accepts and no self-consistent test would catch it | `[MAC]` §2.2.2.4 p. 2-8 | confirmed |
| 25 | **The early acknowledge byte is CRC'd as zeros**: "ring hardware treats this field as a string of Zeros in its CRC calculation", which is what lets a receiver rewrite it in flight without the frame check going stale. The receiver's CRC covers the packet header and data sequences and the separators, ignoring bit-stuffing bits; the *late* acknowledge field is outside it entirely, being neither sequence | `[MAC]` §2.2.2.2 p. 2-8, §2.2.2.4 p. 2-8 | confirmed |
| 25a | Open detail inside 25: §2.2.2.4 says the CRC covers "the separators" without saying **how** a nine-bit out-of-band symbol is fed to a bit-serial CRC. This core feeds all nine bits. No capture exists to settle it, so the choice is marked `PROVISIONAL` in `ap_ring_frame.h` and will stay so until the ring firmware's own CRC routine is disassembled | `[MAC]` §2.2.2.4 p. 2-8 | provisional |
| 26 | Packet data: 0 to 4096 bytes, always even, "typically ... 1024 bytes". The controller may abort after any even byte on error or when the early acknowledge shows nobody is copying, and an abort sets the error bit in the late acknowledge field | `[MAC]` §2.2.2.3 p. 2-8 | confirmed |
| 27 | **Bi-phase, precisely.** Each 83.33 ns bit cell holds a clock window and a data window. "In each clock window, a transition ... must always be present or a bi-phase error will occur **and the corresponding data will be interpreted as having a bit value of Zero**"; in the data window a transition is a One and its absence a Zero. So the encoding is differential -- absolute level carries nothing -- and a corrupted cell yields a *bit*, not a gap, which keeps byte framing intact across a single glitch | `[MAC]` §3.2 p. 3-3 | confirmed |
| 28 | **Network stability is the whole purpose of the elastic store.** "the total delay around the network must be exactly an integral -- rather than a fractional -- number of bit-times", and each node's buffer contributes the fractional part that makes the sum whole. Nominally a **1-bit** delay when a node's transmit and receive PLLs are in phase; the range is `0.5 <= ESB <= 1.5` bits; underflow at "0.5 bit-times or less" and overflow at "1.5 bit-times or more", after which "the network is forced to re-initialize at a new operating frequency (24 MHz)". The bounds are therefore **inclusive failures** | `[MAC]` §3.3, §3.3.2 pp. 3-4 | confirmed |
| 29 | **PLL relationship.** Each node has one receive and one transmit PLL. Receive loops "track over a greater range of frequencies and adjust faster", so a node's receive PLL is always synchronised to the *preceding* node's transmit PLL. The transmit PLL tracks the receive PLL "with a damped response, so most phase jitter never propagates to the next node", and "the transmit phase-lock loop's phase gain is always less than one", which is what makes stability around the ring provable rather than hoped for | `[MAC]` §3.3.1 p. 3-4 | confirmed |
| 30 | **Passive bypass does two things at once**: relays join the node's input coax to its output coax *and* join its transmit output to its receive input. The second half is what lets a bypassed node run loopback self-tests -- so the ring firmware's own self-test, which is this controller's first real test, requires modelling both | `[MAC]` §3.5 p. 3-5 | confirmed |
| 31 | Analogue signal characteristics, recorded but **not modelled**: driver cutoff 18 MHz, receiver sensitivity -20 dBm, maximum 1 km between nodes, bypass insertion loss <= 1 dB | `[MAC]` §3.4 p. 3-5 | confirmed |
| 31a | **An inconsistency in `[MAC]` itself, not a transcription error.** §3.4 gives transmitted power as "18 dBm into 75 ohms (typically, 2.5 V peak-to-peak)". 2.5 V peak-to-peak into 75 ohms is about 10 dBm as a sine or 13 dBm as a square wave -- not 18. Verified against the page image at 220 dpi, so the manual really does say it. Recorded so a later reader who does the same arithmetic does not have to wonder whether we mis-copied it. Nothing depends on either figure | `[MAC]` §3.4 p. 3-5, page image | confirmed (as an inconsistency) |
| 32 | **A ring must be longer than its own token.** A token is nine bits and a transceiving station contributes one bit of delay (`[MAC]` §3.2 with §3.3.2's nominal), so a ring whose total delay is under nine bit-times has its token's head return before its tail has left, and the symbol overwrites itself. Real rings are nowhere near that bound because §3.3 counts "cable plant" as a static delay element and 1 km between nodes is about 60 bit-times at 12 Mbit/s. **This core models the station's delay and not the cable's**, so the bound is reachable here and is a modelling gap rather than a hardware limit | derived from `[MAC]` §3.2, §3.3, §3.3.2 | confirmed (derivation), gap (cable delay) |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | The controller's register map within `0x220`–`0x23F` | **Partly recovered — see findings 11–14 below.** The offsets the firmware touches, their widths and their access directions are now evidenced; what each one *means* is not. Remaining: the meaning of the four word registers at `+400`, and whether `+800`/`+C00` are the dual-ported RAM rather than registers |
| B | Dual-ported RAM buffer: size, host window, and descriptor/queue layout | Same disassembly, plus `[S3K]` AT memory-space table |
| C | Exact token and frame character encodings on the wire | `[MAC]` §2.2.1–2.2.2, transcribed with each format citing its subsection |
| D | Ring latency contributed per node by the elastic-store buffer, and PLL acquisition behaviour | **Partly answered — findings 28 and 29.** The steady-state delay is now evidenced: nominally 1 bit per node, bounded by 0.5 and 1.5, with the offset linear in frequency deviation across ±3 kHz. What remains is *acquisition*: how long a PLL takes to lock, and what the ring does during the re-initialisation that an ESB under/overflow forces. `[MAC]` gives neither; `[PAT575]` is the remaining source |
| E | Token-loss detection, ring reconfiguration and node insertion/removal timing | `[MAC]` ch. 2, `[PAT575]`, and Domain/OS driver behaviour observed under the MAME oracle's *host* side where applicable |
| G | How Domain/OS's single-level store maps onto ring packets — page faults across nodes | `[AEGIS]`, `[ARCH]` |

**H is resolved, and the answer is the negative one it was framed to accept.**
It asked what the 2-byte trailer at offset `length` means. The route named was to
disassemble the boot PROM's option-ROM scan -- "if that scan never touches offset
`length`, the field is not consumed by the machine and the question closes as
*not read by firmware*, which is a sufficient answer for emulation". It never
touches it. `length` is read by **exactly one instruction in the whole 64 KB
PROM** -- `move.l $C(a1),d1` at `01098`, searched for exhaustively as any
`(d16,A1)` operand with displacement `$000C` -- and it is used as a *count*: the
sum runs over `[0, length)` and stops one long word short of the trailer. The
three other `(d16,A0)` reads at that displacement are a vector-table setup and a
list walk, neither in the option-ROM path, which uses `A1` as the image base
throughout.

So the trailer is outside the image, outside the checksum, and outside anything
the firmware reads. It stays unexplained and it is **not a blocker**: an emulated
option ROM need not reproduce a field no code consumes. If a future disassembly
of the *ring* firmware turns out to read it, this reopens -- but the machine that
loads these ROMs does not.

**F is resolved** — see findings 10, 10a and 10b. It asked whether 12 Mbit/s was
the bit rate or the symbol rate, because a non-12 MHz line clock would force
`AP_TIME_BASE_HZ` to be recomputed. It did: the PHY is bi-phase with a 24 MHz
line clock, and the base is now 6.6 GHz. Resolving it before implementing the
medium was the point — the answer changed the unit of account, and changing that
after goldens exist would have invalidated every one of them.

Item D is now the remaining physical-layer unknown, and 10b is the part of it
that the medium will have to face first.

## Divergences from the oracle

None yet: there is no oracle for the ring to diverge from. When the 3c505 802.3
path is implemented, that one *does* have a runnable reference and gets its own
row in `tools/mame-oracle/FINDINGS.md`.
