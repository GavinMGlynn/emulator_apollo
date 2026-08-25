# 3c505 EtherLink Plus — walk coverage record

The DN3500's Ethernet card. `008778-03` §14 places it; `ap_3c505.h` models the
host-side mailbox and deliberately not the adapter's firmware.

| Tag | File | Pages | State |
| --- | --- | --- | --- |
| `[DEV]` | `3com/3c505_Etherlink_Plus_Developers_Guide_May86.pdf` | 77 | **walked whole, 77/77, 2026-08-25** |
| `[HIS]` | `3com/1569-03_EtherLink_Plus_Technical_Reference_Jan89.pdf` | 84 | **32/84 read 2026-08-25**; the rest is `[DEV]` reorganised |

`[DEV]` is Revision 3.0, 3Com document 1569-02, 27 May 1986. Read as 150 dpi
page images, four to a sheet. The book's content ends at page 73.

## Not a hidden-part case, checked before reading

The `[765]`/`[2681]` pattern needs the host's own bus cycles to reach the part.
Here they do not: `[DEV]` §1.2 has the card as "a 16 bit microcomputer" — an
8 MHz 80186 with 16K of firmware and an 82586 behind it — and `ap_3c505.h` says
plainly that the host never touches the 82586. The **mailbox is the contract**,
and `[DEV]`/`[HIS]` are its primary sources. So this is an ordinary walk, and
the Intel 82586 datasheet stays off the critical path.

## Coverage

| § | Yield |
| --- | --- |
| 1.0-1.2 | 8 MHz 80186 no wait states, 82586, 16K-128K EPROM, 128K-512K packet buffer, 8/16-bit host interface, **20-byte FIFO**, on-board thin-Ethernet transceiver, 8K host EPROM |
| 1.3 | **Three address maps.** Adapter I/O: `100` command, `102` control/status, `104` data, `180`-`18F` Ethernet address, `FF00`-`FFFF` PCB. Adapter memory: four 128K banks, ROM at `FC000`-`FFFFF`. **Host I/O: base+0 command, +2 status read / control write, +4 data, +6 control read — factory base `300H`**, which `ap_3c505.h` already derives to physical `058000` through this machine's AT decode |
| 1.4-1.6 | 80186 timing (4-clock, 500 ns bus cycles); 82586 taking ~35% of bus bandwidth when active; the **SEEQ 8023** Manchester codec with a watchdog limiting transmission to 25 ms ≈ 31 kbytes; the AMD 7996 transceiver |
| 1.7-1.8 | Firmware ROM and DRAM refresh — a 15 µs Timer-2-driven DMA cycle refreshing two locations, consuming 3.3% of bandwidth |
| 1.9 | **The host-adapter interface**, and the `DIR`/`HRDY`/`ARDY` truth tables for PIO and DMA in both directions. The Data Register is 20 bytes deep as an 8-bit FIFO and 10 words as a 16-bit one, configured automatically by slot width. §1.9.5's "the Status Flags ... are not decoded by the hardware in any way", which `ap_3c505.h` quotes |
| 1.10-1.11 | Adapter interrupts (Command Register Full on INT0, 82586 on INT1, `ATTN` as NMI) and the two host interrupts (DMA complete via `TCEN`, Command Register Full via `CMDE`), with the warning that both disabled floats the channel |
| 1.12-1.15 | Power-on reset; **`ATTN`+`FLSH` together as a hard reset**, which `ap_3c505.h` models; the Ethernet address PROM at `180H`; two LEDs; the host ROM socket |
| 2.0-2.7 | Every register bit: `HCR` (`ATTN`, `FLSH`, `DMAE`, `DIR`, `TCEN`, `CMDE`, `HSF2`, `HSF1`), `HSR` (`HRDY`, `HCRE`, `ACRF`, `DIR`, `DONE`, `ASF3`-`ASF1`), the Aux DMA register's `BRST`, `ACR` (`LPBK`, `FLSH`, `R586`, `LED2`, `LED1`, `ASF3`-`ASF1`) and `ASR` (`ARDY`, `ACRE`, `HCRF`, `DIR`, `8/16`, `SWTC`, `HSF2`, `HSF1`). All match `ap_3c505.h` |
| 3.0-3.2 | The PCB protocol: 64-byte maximum, the `SF2`/`SF1` states (`00` undefined, `01` accepted, `10` rejected, `11` end of PCB), the 40 ms / 500 µs / 50 ms / 20 ms timeouts, and **Table 1's whole command set** — host `01`-`11`, adapter `31`-`41` — matching `AP_3C505_CMD_*` |
| 3.3 | The ROM utilities, `INT 80H`-`88H`. **Adapter-resident**, for downloaded programs; not host-visible |
| A-E | 80186 peripheral control block values, the 82586 configuration block, the `3C505.EXE` diagnostic's eight tests, the 3D debugger, the developer's diskette. Host tools and firmware internals |
| F-G | **Revision 2.0 and 3.0 ROM changes** — see below |

## The finding: this model has implicitly chosen a firmware revision

Appendices F and G list what changed between ROM revisions, and several are
**host-visible**:

- Rev 1.0 → 2.0: the maximum PCB timeout rises from **127 to 32767 ticks**; the
  timer resolution changes from **25 µs to 15 µs** per tick; `Configure Adapter
  Memory`'s defaults and minimums change; **PCB `11H` Adapter Info and `41H`
  Adapter Info Response are new**; `Self-Test` gains ROM checksum, RAM test and
  82586 loopback; Download Program's PCB loses its offset:segment words.
- Rev 2.0 → 3.0: `Transmit Packet` waits **50 ms instead of 30 ms** for the host
  to download packet data and returns error `-2` if it did not; received packets
  are uploaded in arrival order, which Rev 2.0 did not guarantee.
- And a *hardware* revision difference in the same appendix: "in the latest
  revision 3C505 adapter, the bits in the Adapter Control Register that control
  the LEDs are **inverted** from earlier revision adapters."

`ap_3c505.h` already records the Rev 2 / Rev 3 **hardware** question from
`[HIS]` — the Host Control Register is write-only on Rev 2 and readable on
Rev 3 — and says "the DN3500's card is not yet established either way". The
**firmware** revision is a second axis, and this core has silently taken a
position on it: it implements `AP_3C505_CMD_ADAPTER_INFO = 0x11`, which appendix
F says is **new in Revision 2.0**. So the model is a Rev 2.0-or-later ROM
whether or not anyone decided that.

*Not changed*, because nothing here is wrong — `11H` is a real command on the
revision Apollo most likely shipped, and the timings that differ (127 vs 32767
ticks, 25 vs 15 µs) are not modelled at all. What is worth having is the
statement: **the model implements a Rev 2.0-or-later command set**, and the two
revision axes are independent. Named as a plan item so a later reader meeting
`[HIS]`'s Rev 2/Rev 3 hardware note does not assume it settles the firmware too.

## `[HIS]` — 32 of 84 pages read 2026-08-25, and it completes the revision picture

3Com part 1569-03, published January 1989, © 1988. It is `[DEV]`'s content
**reorganised and revised**: five chapters instead of three, with the register
descriptions promoted into their own Chapter 3, a new Chapter 5 (*Programming*),
and a new **Appendix H, the firmware idle loop listing**.

*Read*: PDF 1-8 (title, contents, figures and tables, Chapter 1), 21-32
(§2-9 to §2-13 and the whole of Chapter 3), 57-64 (§5-3 to §5-5 with Figure 5-1,
Appendix A and B, Appendix C's opening), and 81-84 (Appendix G's tail and all of
Appendix H). *Owed*: Chapter 2's opening (§2-1 to §2-8), Chapter 4, and
Appendices C-G's bodies — which are `[DEV]`'s chapters 1 and 3 and appendices
C-G, walked whole there.

### It answers the hardware-revision question `ap_3c505.h` leaves open

The header records one consequence of the Rev 2 / Rev 3 hardware split, from
`[HIS]` §3-1's footnote: the Host Control Register is write-only on Rev 2 and
readable on Rev 3. **There are at least two more, and they are in this manual:**

- **§3-4, the Host Aux DMA Register**: "This register is cleared upon power-up.
  **It doesn't exist on older Rev 2 hardware boards.**" `ap_3c505.h` maps `+2`
  on write to exactly this register.
- **Appendix A now prints two sets of `dma0` initialisation values**, one
  labelled "(Rev 3 ROM)" and one "(these values for Rev 2)" — where `[DEV]`'s
  Appendix A had a single set, which matches the Rev 2 column.

So the model implements a register absent from Rev 2 hardware, just as it
implements a PCB command new in Rev 2.0 firmware. **Both axes point the same
way**: this core is a Rev 3 board running Rev 2.0-or-later firmware. Neither was
decided; both follow from what was modelled. Recorded in the plan.

### Two passages worth having that `[DEV]` states less fully

**§2-12's soft reset**, which `[DEV]` mentions only in passing: "By setting only
the ATTN bit of the Host Control Register, the host can initiate a soft reset of
the adapter. This reset causes the adapter firmware to clear the Command
Register and any commands that are queued on the adapter, **flush all packet
buffers and queues, and stop any DMA transfers**. The soft reset does not
perform configuration or self-test functions, so does not incur the several
second delay of a hard reset." `ap_3c505.h` models `ATTN`+`FLSH` as the hard
reset; this is the fuller statement of `ATTN` alone.

**§2-12's edge-triggered-PIC hazard**, which is this project's own recent
subject from the other side: "PC and AT type machines used edge triggering mode
on the Intel 8259 PIC. In this mode the Interrupt Request signal must go
inactive sometime after the EOI is issued or **the channel will not be
re-armed**. If both adapter interrupts are enabled it is possible to have a case
where while handling one interrupt type, the other interrupt type occurs and
holds the Adapter's Interrupt Request signal active. The ISR must check for this
and in some way cause the request to go inactive after the EOI is issued **or
interrupts may be lost**." A card vendor documenting the exact failure mode
`ap_i8259`'s edge model produces, and worth citing next to it.

### Appendix H, new in this edition

A commented assembly listing of `cmd_processor`, the firmware's main loop, with
the PCB dispatch spelled out per command code — `01`-`03` and `0a`-`0c` and
`0e`-`11` enqueued via `INT 88`, `04`-`07` processed directly via `INT 80`, `08`
enqueued as a receive via `INT 86-1`, `09` fetching packet data then enqueueing
a transmit PCB. Not modelled and not needed while the mailbox is the contract,
but it is the only published description of *why* a given PCB's response timing
differs, and would be the starting point if the firmware were ever emulated.
