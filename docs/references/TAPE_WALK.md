# SC-499 and QIC-36 — walk coverage record

The DN3500's cartridge tape controller and Apollo's own specification for it.
`QIC-02` — the command-set standard both rest on — is walked whole and recorded
separately in `QIC-02_WALK.md`.

| Tag | File | Pages | State |
| --- | --- | --- | --- |
| `[SC499]` | `archive/Archive_SC-499_Tape_Controller_Information_Guide.pdf` | 42 | **walked whole, 42/42, 2026-08-25** |
| `[08845]` | `archive/08845_Apollo_Specification_for_QIC-36_Tape_Controller_Jan86.pdf` | 44 | **walked whole, 44/44, 2026-08-25** |

Both read as 150 dpi page images, four to a sheet. Both carry a previous
reader's pen annotations, which are *not* part of the document and are recorded
here only where one flags a real question.

## What the pair is, and why it is not a hidden-part case

Checked against the `[765]`/`[2681]` pattern before reading: it does not fit.
The host addresses the SC-499's **own** four registers directly, and `[SC499]`
is that controller's own guide — there is no part underneath whose datasheet
outranks it. The drive behind is QIC-02/QIC-36, and `QIC-02` is walked whole.
So these are ordinary whole-document walks.

**`[08845]` is the more interesting of the two**: it is *Apollo's* specification,
drawing 008845 Rev E0, dated 23 January 1986, and its §11 reproduces `[SC499]`'s
§1.9-§1.12 almost word for word. Where it differs, it differs because Apollo is
specifying **its own configuration** of the vendor's board — which is exactly
the class of statement a vendor guide cannot make.

## `[SC499]` coverage

| § | Yield |
| --- | --- |
| 1.1-1.5 | QIC-02 command set, QIC-36 drive interface, QIC-24 format; **Table 1-1's jumpers** — base address selectable 0 to 3F8 on 8-byte boundaries (factory 200), DMA on DRQ1/2/3, interrupt on IRQ2-7. `RR` is listed with **"No Description ... For Archive use only"** |
| 1.6-1.8 | TTL levels, the J1 ISA pinout, the J2/J3 drive pinouts (GO, REV, TR0-TR3, RST, DSO, HC, RDP, UTH, LTH, CIN, USF, TCH, WDA, HSD, WEN, EEN), power, installation |
| 1.8.1 | **POC test**: microprocessor RAM, LSI controller, 16K RAM, data separator; success reported "by the assertion of `EXC-` **within five seconds**"; five LEDs, one blink each on success |
| 1.9 | The four registers — `+0` data/command, `+1` control write / status read, `+2` DMAGO, `+3` RSTDMA — and both register layouts. All four are `ap_sc499.h`'s |
| 1.10 | Interrupts: each source readable "regardless of the state of the interrupt masks"; **the IRQ line is tri-stated when IEN is cleared**, which `ap_sc499_irq` implements as absence rather than inactivity |
| 1.11 | DMA via the host 8237, and the five-step transfer sequence |
| 1.12 | **CORRECTED 2026-09-09.** Four resets, and the row below recorded the hold requirement while passing over the **NOTE printed between the two lists**: *"Microprocessor RESET will also cause a tape drive reset."* Microprocessor RESET is caused by either supply rail dropping **or by RSTSAC being set**, so a host that pulses RSTSAC resets the *drive* too — it comes back at load point holding `POR`, selected by default. This core reset the controller alone, so a host got a drive left exactly where it was: measured on the SR10.4 cartridge boot as `bad rewind` with the drive at block 98,263 and its exception word `0000` (`FINDINGS.md` C270). `RSTSAC` must still be set, held for more than 25 µsec, then cleared |
| | *original row* | *Four resets. **`RSTSAC` must be set, held for more than 25 µsec, then cleared*** |
| 1.13-1.13.2 | The twelve QIC-02 commands with opcodes, and Figures 1-5 to 1-10's interface timing — `T4→T5 < 500 ms` for a command's READY, `20 µs < … < 100 µs` for its deassertion, `< 3 µs` acknowledge deassert |
| 1.13.3 | **CORRECTED 2026-09-09.** Sixteen driver flow charts (Figures 1-11 to 1-26), and **two of them are the specification of this interface's handshake, not commentary on it**. Figure 1-25, READ STATUS: `READY?` → `READ DATA BUS` → `ASSERT REQ` → 20 µs → `READY?` *looping while the answer is yes* → `DROP REQ` → `ALL 6 BYTES?`. Figure 1-26, SEND COMMAND: `READY?`/`EXCEPTION?` → byte to the bus → `ASSERT REQUEST` → `READY?` → `DROP REQUEST` → `READY?*` *looping while yes*, footnoted "20 µsec loop max, see timing". Both end by waiting for READY to **go away** — `QIC-02` §3.6.3's T7 — which `ap_sc499` never produced, and which is the spin the SR10.4 boot firmware dies in (`FINDINGS.md` C264). Figure 1-23's RESET chart shows the 25 µs hold |
| | *original row, kept because it is what the reading stood on* | *Sixteen driver flow charts (Figures 1-11 to 1-26). **Driver-side, not part behaviour**; Figure 1-23's RESET chart shows the 25 µs hold, and Figure 1-26 notes a "20 µsec loop max". — The dismissal and its own counter-example are in one sentence: a driver's loop **maximum** is a statement about what the part must do inside it.* |

## `[08845]` coverage, and the three things only Apollo can say

Sections 1-10 are procurement: environment, power (1.0 A at +5 V typical),
mechanical, agency compliance, 400,000-hour MTBF. §11 is `[SC499]` §1.9-§1.12
reproduced. §12 is the QIC-02 command set again, and Appendix A the flow charts
again. What is *new* is Apollo's own configuration and its own timings.

**1. The Apollo jumper configuration, §5.3 and Table 2.0.** The table marks the
as-shipped vendor setting with a dot and **Apollo's own with an asterisk**:

    Device (base) address     0200 HEX     A3-A8 OUT, A9 IN
    DMA channel               1            DRQ1/DACK1
    Interrupt request level   5            IRQ5

and — the one that matters — a jumper `[SC499]` does not name at all:

    RR   READY INTERRUPT DISABLE    Apollo: IN

`[SC499]`'s Table 1-1 lists `RR` under "No Description — For Archive use only".
Apollo names it and **straps it IN**, which the annotator glosses "READY
DISABLED - NOT ON INT".

**2. §12.3's command maximum timings** — already derived. `AP_SC499_MAX_RESET`,
`MAX_BOT`, `MAX_RETENSION` and `MAX_ERASE` carry 5 s, 1 min 20 s, 241 s and
4 min with `08845` §12.3 cited, so this document had been consulted for that
table before this walk. (It is why the earlier citation audit saw a stray
"§12.3" against `ap_sc499`.)

**3. §11.6's interrupt causes**, which `[SC499]` does not enumerate: "1. After
completion of any command when status is valid and ready to be checked. 2. **A
soft reset has been issued.** 3. An exception condition occurs such as illegal
command or an erroneous condition occurs during the operation of the tape
drive."

Also from §6.3, Apollo's performance figures for the pair: 90 KB/s, 9-track
serpentine, (0,2) RLL GCR, **3 × 512-byte block buffering**, 16 write/read
retries, CRC.

## The finding: `RDY` raises `IRQF` here, and Apollo's board straps it off

`ap_sc499.c`'s `interrupt_flag` returns true when `ready` is set, from
`[SC499]`'s "IRQF — Interrupt Request Flag, ORing of RDY AND EXC, and DONE if
DNIEN is set". That is the vendor's default, and it is what `RR` **OUT** gives.
`[08845]` Table 2.0 has Apollo strapping `RR` **IN**, disabling the ready
interrupt — so on an Apollo board a ready condition should not raise `IRQF` and
only `EXC` (and `DONE` under `DNIEN`) should.

**Not changed, and the reason is which machine the document describes.**
`[08845]` is a **DN3000** specification: §5.2 has the board plugging "into any
I/O expansion slot of an Apollo DN3000 system" and §13.2 says it "will operate
only in an Apollo DN3000 system backplane". Its base address is `0200`, and
`008778-03` Table 2-9 places the **DN3500's** tape at AT `218`-`21F` — a
different strap on `A3`-`A9`, so the board is jumpered differently on this
machine and the `RR` column cannot simply be carried across.

**What would settle it**: a DN3500-era Apollo tape controller specification, or
a boot in which the tape signals ready with `IEN` set and nothing else pending —
which the existing interrupt counters would show. Named as a plan item rather
than changed, because acting on a DN3000 jumper table would be modelling a board
this machine does not have, and the current behaviour is the vendor default
rather than an invention.
