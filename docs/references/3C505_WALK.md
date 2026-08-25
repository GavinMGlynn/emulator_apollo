# 3c505 EtherLink Plus — walk coverage record

The DN3500's Ethernet card. `008778-03` §14 places it; `ap_3c505.h` models the
host-side mailbox and deliberately not the adapter's firmware.

| Tag | File | Pages | State |
| --- | --- | --- | --- |
| `[DEV]` | `3com/3c505_Etherlink_Plus_Developers_Guide_May86.pdf` | 77 | **walked whole, 77/77, 2026-08-25** |
| `[HIS]` | `3com/1569-03_EtherLink_Plus_Technical_Reference_Jan89.pdf` | 84 | **owed** |

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

## Owed

`[HIS]`, the 1989 *EtherLink Plus Technical Reference*, 84 pages — the sibling
`ap_3c505.h` already cites for §2-3 and §3-1's register map and for the
write-only-on-Rev-2 footnote. It is the later and more authoritative of the two
and has **not** been walked; the register-map defect recorded in `ap_3c505.h`
("found by the sibling-manual step, on a document that had been on disk for a
day") came from querying it, not from reading it through.
