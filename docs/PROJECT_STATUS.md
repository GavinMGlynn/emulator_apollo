# Project status

The single source of truth for **what works**. Updated in the same commit as the
code it describes. If this file and the code disagree, the file is the bug.

**Accuracy claim: none yet, and the reason is now specific rather than
general.** What exists is a 68030 that executes, a core board that answers, and
— as of this update — a Domain/OS SR10.4 system installed and booting under the
oracle, which is the reference the accuracy claim will eventually be made
against. None of that is a timing claim, and nothing timed may be measured
through the install, which is paced by the host.

**What runs.** `ap_m68030_step` fetches through the pipe and the instruction
cache, decodes, executes, takes exceptions and advances the PC. The opcode map
has no undecoded holes left. Executing today: everything in families `0000`
through `1111` except `BKPT`, `CAS`, `CAS2`, `CMP2`, `CHK2` and the coprocessor
instructions other than the MMU's — so the six ALU operations in both
directions, the immediate forms, `MOVE`/`MOVEA`/`MOVEM`, the quick and
conditional forms, the shifts and rotates, the multiplies and divides, the
extended and BCD forms, all of family `0100` bar `BKPT`, the whole `$4E` control
group, and `PMOVE`/`PFLUSH`/`PFLUSHA`/`PLOAD`/`PTEST`. Every addressing mode the
part has resolves, full-format indexed and memory indirect included, and mode
legality is enforced by category rather than approximated. Exceptions are taken
and returned from: `TRAP`, `TRAPV`, `CHK`, divide-by-zero, `ILLEGAL`, privilege
violations, format errors, MMU configuration errors, interrupts, trace,
**bus error**, **address error**, and the **line 1010 and line 1111 emulator
traps**.

The last four were added while running the boot PROM and each closed a real
gap. A faulting access used to report `UNIMPLEMENTED`, blaming the processor for
the memory system's answer. An odd instruction prefetch was not detected at all.
And an `A`-line or `F`-line word — which is what an empty AT bus slot reads —
reported unimplemented, when taking the trap *is* the complete behaviour and
there is nothing left to implement.

An instruction outside that set reports `UNIMPLEMENTED` rather than silently
succeeding, so "how far a program got" is a real measure.

**What the clock covers, and what it still does not.** The accumulated clock
covers bus and cache time — prefetches, operand accesses, table searches and
line fills, each priced by the bus and cache modules against cited pages — plus
instruction execution time for the **59 transcribed rows** of `[030]` §11.6,
scheduled against the bus rather than added to it, which reproduces both the
`CC` and `NCC` columns of every one of them.

The rule is *not* `max(microcode, bus)`. That was the first model and it was
retired before the effective address tables were transcribed — `max` is
monotonic in both arguments, while warm and cold `ADD.B D0,(A0)` need answers
that move the opposite way to their bus times, so no microcode figure reaches
both. `docs/references/M68030_TIMING.md` carries the arithmetic under
"`max(microcode, bus)` does not survive the effective address tables".

What is implemented asks one question per bus cycle — *is the microcode waiting
on this?* A prefetch is not waited on and can hide; an operand read the
operation is about to consume cannot. So the published cache case is split into
the microcode it leaves exposed and the operand bus it assumes, the measured bus
is substituted for the assumed one, and the prefetch is charged only for the
part that did not hide: **exposed microcode + measured operand bus + prefetch
exposure**. That is what makes a wait state or a cache hit move the answer,
which a table lookup could not.

`ap_m68030_schedule` still computes plain `max` and is still tested, because the
overlap module's own worked examples are stated in those terms — but nothing in
the step calls it, and it is not the model the machine runs.

So a register-to-register `ADD` costs its published 2 clocks. An instruction
outside those 59 still costs bus time alone and is a lower bound, and
`--time-instructions` shows which is which: a scheduled instruction reads
steady, an unscheduled one alternates 0/2.

The harness to measure against exists on both sides
(`tools/mame-oracle/steptime.lua`, and `--time-instructions` on ours), and seven
instructions agree with the oracle. The 0/2 alternation is classified rather
than a defect: it is §11.3.3's "one external bus cycle per two instruction
prefetches", where the published tables average the two alignment cases and the
oracle reports a constant. Ours is hardware-truer there and is kept
(`FINDINGS.md` C7).

What remains open is **composition**. Rows footnoted "Add Fetch Effective
Address Time" are declined rather than part-priced: their published figure is a
component, and Equation (11-2) needs a model that can hide *part* of a bus cycle
(`FINDINGS.md` C9). Transcribing more of §11.6 does not close it — as
`docs/references/M68030_TIMING.md` records, no published NCC number is a value
any single execution ever takes, so a core that looked them up would be
reproducing an average the hardware never exhibits.

**The boot PROM runs.** `--boot-prom <image>` loads the DN3500's own PROM at
address zero, takes the reset stack and program counter from its first two long
words, and runs. Twenty instructions execute with **zero bus errors and zero
unmapped accesses** — the first independent check on the address map by something
other than a test written beside it — and with the immediate-to-status-register group now implemented it reaches **35**.

A faulting access now **takes** the bus error exception rather than stopping the
step, which is what the real part does and what firmware depends on — an
undecoded read is how a probe *asks* whether a card is present. It reaches **89
instructions**: the fault at `000028D0` is taken, the PROM's own handler at
`00000404` runs, and the run ends in a **double fault** when the exception stack
runs off the bottom of main memory. That is the real part's behaviour, and the
run is bounded and deterministic.

Getting there needed a defect fixed one layer down. `ap_m68030_store_fn` returned
`void`, so the memory system could *count* a write to an address nothing decoded
and then had no way to refuse it — no write could ever raise a bus error. A
signal a callee cannot send is one the caller assumes never happens. With the
store able to say no, an undecoded write faults like a read of the same address,
and the fault loop ends in a double fault instead of running forever.

That fault was a **missing device, not a defect**. A real DN3500 decodes the
display controller registers and answers at `0005E801`; ours did not, so the
firmware took an exception the real machine never takes and everything
downstream of it looked like a bug in the exception path (`FINDINGS.md` C32).

With the controller's identification modelled the probe is answered and the PROM
reaches **425 instructions**. It now reaches `00090000`, which is **AT bus memory** rather than unmapped: both
AT windows are decoded by the board, and an address with no card behind it reads
`FF` and terminates normally. The PROM jumps there to scan for an expansion ROM,
so a board that faulted on an empty window would turn "found nothing" into a
crash — the display controller's lesson a second time, found the same way.

Reading `FF` gives `FFFF`, an F-line word, and with the **line 1010 and line
1111 emulator traps** now raised the machine takes vector 11 the way the
hardware does. **The boot PROM executes 57 instructions of real work, then runs away.**

An earlier reading of this line said 5000000 instructions with zero bus errors,
and that was wrong in the way most worth guarding against: it was true and
meaningless. At step 57 an `RTS` at `00002946` returns to `00000000`, and
everything after is the machine walking the vector table as `ORI.B` instructions
forever. It never faults, because `ORI.B` on D0 over readable PROM is harmless —
which is precisely why five million clean instructions looked like progress.

A zero-fault count is not evidence of a boot. It is evidence that nothing
complained.

The stack accounting around the bad `RTS` is correct — every push and pop moves
A7 by four and they balance. Tracing A6 refuted the overlap hypothesis: A6 is
`01000180`, the firmware indexes it with positive offsets, and the stack grows
down from the same address by design.

What the trace does show is that step 10 is a `JMP`, not a `BSR`, and that the
slot the bad `RTS` reads was never written by anything — RAM starts zeroed. Walking those 46 steps
showed the control flow following the PROM exactly, every `BSR`/`RTS` pair
balancing — so it is not a branch either.

What it did surface: step 18 is `MOVE.W SR,-(A7)`, a *word* push, which leaves
every later long push and pop at **2 mod 4**. The failing read is a long at
`01000172`, spanning the cache lines at `01000170` and `01000174`. That hypothesis is
**refuted**: tested directly, a long at 2 mod 4 round-trips through the data
cache even after a 4 KB sweep evicts around it. The test is kept, since
misaligned data is legal on this part and the path deserves cover regardless.

Watching the location settled it. **The memory is correct**: `01000172` holds
`00000620` from step 30 through step 57. The `RTS` read the right address, the
right value was there, and it jumped to zero.

It was the **data cache**, and half of it is fixed. A cache entry is a whole
long word, so any access that is not an aligned long word spans two entries. The
write hit path updated one entry with a long assembled from the wrong bytes and
left the other stale. Both are now invalidated on a misaligned long write, which
costs a refill and cannot return a wrong value since writethrough has already
reached memory.

The read path was never the problem. `operand_write` splits at long-word
boundaries, so a misaligned long reaches the cache as two *partial* writes, and
the hit path stored a partial value into a four-byte entry — replacing bytes it
had not written. The written bytes are now merged into their own lanes.

**The runaway is gone.** The PROM no longer reaches address zero, and 300000
instructions leave the PC inside the PROM with 129 bus errors — **all of which
are its own self-test**. MAME's `apollo_unmapped_r` calls `apollo_bus_error()`,
so an unmapped read does fault on a DN3500 and ours matches, and its source
names `00030000` as the "Bus error test address in DN3500 boot prom and
self_test". A machine taking no bus errors here would be the suspicious one
(`FINDINGS.md` C33).

Two clean-looking numbers have now misled this investigation in opposite
directions: five million instructions with zero faults was a runaway, and 129
faults is a self-test passing. The board therefore records the *first* unmapped
address in each direction as well as the count, because a count alone cannot
tell those apart. **The PROM now needs time to pass.** It reaches `000007AE` and stays there at
300000, 1000000 and 3000000 instructions with the fault count settled at 129:
`BTST #0,($102,A0)` and a `BEQ` back, a status-poll loop waiting for a device
bit. `A0` is `00010401`, so the polled address is `00010503`: **SIO2**, register 1
after the decode's shift — the MC68681's status register A, bit 0 of which is
**RxRDY**. The firmware is waiting for a character on the second serial port,
and the apparent timeout counter above the loop is reloaded every pass, so the
wait is unbounded.

Both the firmware and the machine are behaving correctly. What was missing is a
*character*: `src/frontend/headless` has no host input by design.

`--boot-input TEXT` now delivers a byte sequence to SIO2 channel A as the
firmware takes each one — decided before the run starts, so determinism is
untouched. Delivery **retries until the receiver accepts**, because a DUART
whose receiver is still disabled drops the byte and the firmware enables it long
after reset. With a newline the PROM leaves `000007AE` and reaches `00000794`;
without input it stops exactly where it did.

That loop polls **both** ports — SIO1's status register A and SIO2's — so it is
a console read waiting for a character on either, and it is responsive:
different inputs leave the PC at different points inside it. `--boot-console` now prints what the machine transmits, drained from both ports
and both channels every step, and `sio_suite` proves the path in both
directions through the registers a program writes and reads.

**The PROM is silent.** 300000 instructions produce nothing on either port. The
path being tested is what makes that a fact about the firmware rather than about
us — a silent run would otherwise be ambiguous between "has not printed" and
"cannot print". Per-region access counts settle why. Over 300000 instructions the serial region
takes **250244 reads and 38 writes**: the firmware configures both DUARTs — 38
writes is mode, clock-select and command registers for four channels — and then
polls, never transmitting. It is waiting for a console character before
announcing itself.

The same table shows what has *not* been touched, which a total cannot: no timer
and no calendar accesses at all, and the interrupt controllers written ten times
but never read. Whether that is expected this early is not yet established.

**SIO1 is the console, not SIO2.** The poll tests both DUARTs and branches
differently for each, and every run until now fed the wrong one. Feeding a
newline to port 1 moves the PROM to `0000220C`, serial writes go from 38 to
**11839**, main memory writes from 43328 to 177894 and core register writes from
7 to 2368 — substantial work that was not happening before.

No console bytes emerge even so, and per-register counts settle why: **the two
transmit buffers have zero writes on both ports.** The PROM never transmits.
The capture was correct and there was nothing to capture.

What it writes instead is the auxiliary control and clock-select registers,
thousands of times, with the counter/timer preload registers written once each.
That looked like something driving the DUART's counter/timer. It is not:
per-register *read* counts — which carry more than writes on this part, since
reading register 14 starts the counter and 15 stops it — show the counter
registers with **zero reads on both ports**.

What it is instead is a **write-only loop**, about 2362 iterations of one
auxiliary-control write and two clock-select writes, reading nothing back. Reading the loop settles it differently again. `0000220C` is three
instructions — `CMP.B (d8,PC,Xn),D1`, `BEQ`, `DBF` — a table search against
`000021D2`, and that table interleaves high-bit bytes with ASCII: the signature
of a **keyboard scan-code map**, not a command table.

The oracle confirms it and names the channel: MAME's DN3500 wires the keyboard
to serial 1 **channel A** and a terminal to serial 1 **channel B**. So the
reading was right about the device and incomplete about the consequence — ASCII
belongs on a *channel*, not just a port, and every run until now fed channel A.

`--boot-input-channel` now selects it, and feeding `\r` to channel B moves the
PROM to `00002542`, another new region. The input is being consumed.

It still never transmits, and that is now **established rather than suspected**:
MAME's `dn3500()` wires its stdio terminal only inside `#ifdef APOLLO_XXL`, so a
stock DN3500 has no serial terminal at all — just the keyboard on channel A.
Nothing consumes the SIO's transmit. The firmware has nowhere to print because
there is no terminal, not because it is stuck.

The graphics memories now decode — `0A0000-0BFFFF` colour, `FA0000-FDFFFF`
monochrome — and, importantly, *before* the AT bus windows. Both sit inside the
AT memory window, so the board had been reporting the machine's own frame buffer
as an empty expansion slot. In the PROM run 384 accesses move from "AT bus
(empty slot)" to "display controller": the firmware was touching its frame
buffer and we were mislabelling it. No device suite could have caught that —
they call the device directly and the device was right; only a test of the map
sees it.

`--screen c4p|c8p|19i|15i` fits one, allocating the graphics memories in the
frontend and only when a screen is present. The firmware behaves differently per
type — `19i` reaches `00000798`, `c8p` reaches `000046BC` — which is the check
that the ID register is being read and believed. With `c8p` fitted the display
controller takes **803 writes**, up from zero: the firmware initialises a
display it has found, and every one of those writes previously had nowhere to
go. They are to registers this module does not model, which is exactly the gap
the controller proper has to fill.

**The display controller is therefore the next module**, for a reason rather
than as the next item on a list: the four regions already recorded stop being
probe targets and become the machine's output. The matching input module is the
keyboard — serial 1 channel A takes scan codes, and the PROM's table at
`000021D2` is the map it decodes them with.

The tick loop is still owed and remains the project's central design item, but
it is not what this stop needs.

Every board counter now records its first address, not only its count. The AT
bus empty-slot scan begins at `00080000`, exactly the base of AT bus memory, so
the firmware's 15872 empty-slot reads are a systematic sweep for an expansion
ROM — shown rather than assumed.

The first unmapped read is `FFF90000`, and that one is also correct: `F8000000-FFFFFFFF` is
labelled "used by fpa and/or color7?" in the oracle's source, whose handler is
commented *out* of the map — so a DN3500 bus errors there, as we do. The
firmware is probing for a floating-point accelerator and being told there is
none.

Getting there needed `--boot-trace` (PC and A7 per step) and one fix. A7 was the
observable: the PROM's `CLR.B $00011600` bus errored on every pass through its
reset path, each fault drained a frame off a 384-byte supervisor stack, and 2788
instructions later the stack ran past `01000000` and an `RTS` popped garbage. The
cause and the symptom were thousands of instructions apart. `011600` is the
master request register and `011300` the latch-page register; `ap_boardreg.h`
defined both, and the map routed only the four contiguous registers at
`010000-0103FF`. Two registers existed, had passing tests, and were unreachable
through the machine.

Both traps were defined and classified and simply never taken; reporting them
`UNIMPLEMENTED` said the gap was ours when taking the trap is the whole
behaviour. An unimplemented *MMU* instruction still reports `UNIMPLEMENTED`,
because the MMU is fitted and the real part would execute it — that one is our
gap, and it must not be dressed up as an exception the hardware takes.

It previously stopped at `000028D0`, and the investigation of that stop found a defect in the
reporting rather than in the CPU: the step was **reporting a bus fault as an
unimplemented instruction**, because executors signal both with a bare `false`.
The `CMPI.B` there was implemented and correct all along. `access_faulted` now
carries the distinction from the access to the status, and the PROM reports
`FAULT` at the same PC with **the instruction count unchanged** — the fix changes
what the machine says about itself, not what it does (`FINDINGS.md` C30).

The faulting address, `0005E801`, is now confirmed against the oracle: the
firmware is **probing for a display controller**. The addresses are Apollo's own
colour and monochrome controller register blocks (`05E800` and `05D800`), and
the `000A0000` stored alongside is Apollo's colour graphics memory base.

An earlier reading derived the same conclusion by putting those addresses through
the AT window rule, landing on PC MDA and CGA. That derivation was **wrong** —
the ranges are `0x408` bytes, which no `0x80`-strided AT port can be — and it is
kept in `FINDINGS.md` C31 because of *why* it felt safe: three facts appeared to
agree independently, and all three were consequences of one design decision.

Nothing is mis-implemented. The config we boot has no graphics controller, so a
bus error is the correct answer to the probe. The controllers are a new module,
listed in `docs/COMPLETION_PLAN.md`.

**Real Apollo firmware runs.** `--boot-tape <cartridge>` reads a Domain/OS `.ct`
cartridge, extracts its boot image, places it at the load address the image
declares, and runs from its entry point. On flat RAM the SR10.3.5 boot cartridge executed
**16,933 instructions** before faulting — identical count, identical stop reason
and identical state hash across repeated runs and between `-O0` and `-O3`.

Routed through the DN3500's **real address map** it executes **zero**: the
image's declared load address `0013D800` is in AT-compatible bus memory space per
Table 2-8, not main memory, so it cannot be placed. The oracle confirms it: `0013D800` reads `FF` on a real DN3500, main memory is at `01000000`, and `TC = 0`. So this core's map is right, the zero is correct behaviour, and the image's addresses are **logical** — something enables translation before loading it. Both figures are honest and the second is the more informative.

That is not a boot. The fault on flat RAM is expected: this is the boot image on flat RAM
with a chosen stack, no boot PROM, and none of the core-board devices mapped, so
the firmware runs until it reaches for hardware that is not there. What it does
establish is that 16,933 instructions of real 68030 code — not probes, not
synthesised tests — decode and execute deterministically. It is also the first
end-to-end measure of how far the firmware gets, so it is a number that can be
driven upward.

**Two machines exist, and both are used.** `ap_machine` wires the 68030 to flat
RAM: construct, poke, run to a limit, read back. That is what a side-loaded
probe needs and it requires no firmware — built ahead of the boot-PROM route
because that route was then in doubt (`tools/mame-oracle/FINDINGS.md` C4).

`ap_board` is the DN3500 itself, and `ap_machine_set_board` routes the
processor through it. The doubt is resolved: the boot PROM runs from its own
reset vector through the real address map, and the probes keep their flat memory
because a probe harness that had to be a whole DN3500 would be a worse probe
harness.

End-to-end *timing* still cannot be measured, but for a narrower reason than
"no device": every device answers at a fixed two-clock `STERM`, so a slow one
cannot lengthen a cycle. That is the arrival-clock item in Phase 2, not an
absence of hardware.

**Bus arbitration is complete on both sides.** The processor's own BR/BG/BGACK
state machine (`[030]` §7.7.4) plugs into a shared arbitration point that
implements the external priority encoding §7.7 says the board must supply — DRQ0
highest through DRQ7 lowest, and the processor beneath all of them.

Contention is therefore **emergent and measured, not modelled**: nothing adds a
penalty anywhere, and a test simply counts how many of a hundred clocks the
processor was not the bus master while a device held DRQ0. The processor losing
a clock is it losing an arbitration, which is the property this whole design
exists for.

**The core board is largely populated.** Phase 3's devices are in, each placed
by measurement rather than by assumption: the two 8259A interrupt controllers
(cascaded on IR3, not the AT's IR2), the MC6840 interval timer, the MC146818A
calendar, the two 8237A DMA controllers, the two 2681 serial ports, the address
translation map, the node ID PROM, and the four core-board registers that could
be characterised. Every one of their placements differs from at least one
neighbour's — four adjacent pairs on this board have different strides, and one
pair shares a stride but differs in what the odd byte does — so none was inferred
and all were measured.

What is *not* here is anything that needs a running bus or a wire: DMA
transfers, serial framing, the keyboard. Those are named in
`docs/COMPLETION_PLAN.md` with what each waits on.

The first board subsystem was the **address translation map** at `017000`.
It is not the CPU's MMU and does not overlap it — it sits between the AT bus and
physical memory, and exists because a DMA controller has no MMU of its own. The
8237 drives a flat 64/128 KB address and expects contiguous memory behind it,
while the operating system has pages scattered across physical RAM; the map is
what reconciles those, and it is also the 512 KB window an external AT bus
master reaches main memory through. Present on DN3500/4500/5500, absent on
DN3000, and that difference is now a model-table field rather than a conditional.
The golden regression harness now pins **emulated behaviour** as well as the
model table: `--run-probes` runs eight probes on the constructed machine and its
report is a committed golden, checked under every build preset. This section
will state exactly what backs the claim when there is one.

Last updated: 2026-08-02 — Domain/OS SR10.4 installed and booted from its own
disk, closing the first-boot gate; the completion plan's finished items
summarised, with their reasoning moved to the end of this file.

## Domain/OS SR10.4 is installed, and boots from its own disk

The gate this file carried from the start — *"no bootable Domain/OS media: all
we hold is installation media"* — is closed. There is now an installed system on
a disk built from nothing but the five distribution cartridges.

**What it is.** `media/dn3500-sr10.4-installed.awd`, 348 Mbyte of which
263,408,657 bytes are non-zero, against a blank image that was entirely zero.
Gitignored and it stays that way: we made the volume, its contents are Apollo's.
It is pinned instead by SHA-256 in `docs/references/DOMAINOS_IMAGE.md`, together
with the SHA-256 of all five source cartridges, so a rebuild can be checked and
anyone holding the same media can confirm theirs matches before starting.

**How it was made.** INVOL options 7, 1 and 8 to initialise the volume; the
calendar; `ex domain_os` to restore the Phase II environment; then `MINST` with
the `large` template, which loaded 10,992 files from four cartridges and
hard-linked 8,452 of them into place with 3,687 soft links. The command file
that drove it is `tools/mame-oracle/install-domainos.cmds`.

**That it boots is checked rather than assumed**, and by three things that a
merely-noisy console could not fake:

- `error: sysboot not found` is **gone**. That is exactly what `ex domain_os`
  from disk answered before `MINST` ran: the boot-volume restore leaves
  `sysboot.m68k` in the filesystem and the PROM wants `sysboot`, which the
  install creates. The same command, on either side of the thing that was
  supposed to fix it.
- The kernel is a **different image** — it loads to `010E986C` from disk where
  the cartridge loaded to `01111FFF`.
- The Phase II banner carries **no `RBAK version` suffix**, so it is the
  installed environment rather than the restore tool's.

Logged in over the serial console as `user`, `bldt` reports
`**** Node 12345 **** "//node_12345"` — the name INVOL gave the volume, so the
node runs under the identity this project created for it.

**Three rules were learned the expensive way** and each is now in the procedure:

- **No `re` between stages.** `apollo_state::machine_reset` shifts the RTC year
  on *every* reset rather than once at power-on, and the kernel refuses to boot
  when the calendar is behind the volume's timestamp.
- **Change a cartridge only when the drive is idle**, waiting for the complete
  prompt rather than the first words of it.
- **Checkpoint before every irreversible step, and shut down cleanly before
  copying.** A checkpoint taken from a live volume asks to be salvaged on next
  boot; one taken after `shut` does not. Three separate sessions were saved by a
  checkpoint, and one was lost to a checkpoint taken while the machine ran.

**What it does not claim.** The install is a conversation paced by the host and
the volume carries timestamps, so a rebuild is not expected to be bit-identical
and nothing timed may be measured through an image built this way. The hash
identifies *this* image; it is not a reproducibility claim.

## Where the phases stand

**Phase 1 (verification infrastructure) is done.** Every item states a
verification and every one is met: MAME builds and boots Domain/OS to a login
prompt, the oracle harness produces byte-identical dumps from two runs of the
same workload, `FINDINGS.md` classifies each campaign, the probe encoder runs one
program identically on both sides, the probes side-load with `roms/` absent,
`regress.py` pins goldens on every platform and build type, and the
**full-machine state hash** now covers the board as well as the processor.

The last of those was the one item whose remainder was written into the plan as
"the board half", and it stayed open for the right reason: there were no devices
to fold in. There are now, so it closed with them rather than being faked at the
time it was written. What that means concretely is below, under *The board half
of the state hash*.

Phase 1 being finished is worth stating plainly for what it buys rather than as
a milestone: everything after it can be checked. A subsystem landing in Phase 3
gets a golden, a probe against the oracle, and one number that says whether two
runs of it were the same run.

**Phase 2 (the MC68030) is done — and this paragraph used to say so before it
was true.** Every item states a verification and every one is *now* met, which
is worth distinguishing from the tick count, because twice this claim was made
on the ticks alone and twice an audit of the verification lines found one
unmet. The 68882's asked for a probe suite against the oracle: there was none,
and the coprocessor had never been attached to a running machine at all. The
exception item's asked for probes that deliberately fault, diffed against the
oracle: there were none, and *no* built-in probe had ever been run against the
oracle. Both are closed now, by measurement rather than by assertion —
`FINDINGS.md` C59 to C82 — and the campaign runs as one command so the next
person does not have to take this paragraph's word for it.

The lesson is recorded rather than tidied away: **in this plan, a tick has not
reliably meant the verification happened.** Auditing verification lines rather
than counting boxes is cheap and has a hit rate worth the time.

**All eight of Phase 2 and Phase 2b's externally-dependent verification lines
have now been traced**, so a fourth audit does not have to re-derive them:

| line | settled by |
| --- | --- |
| 68030 integer core, "probes against the oracle" | C59-C84, every `ap_probe.c` class |
| exceptions, "probes that deliberately fault" | C73 illegal instruction, C74 long bus fault frame |
| 68882, "probe suite over each operation and rounding mode" | C59-C71, with C70's divergence class |
| 68020, "`dn3000` boots under both; oracle diff" | C84 for the diff; the boot half is Phase 4's, recorded there |
| the two `probe_compare.py` lines | run, and re-runnable as `--program all` |
| timing, "`timing_suite` and the probe goldens" | goldens pinned on every platform and build type |
| multiplies and divides, "then probes for the timing" | **deferred, and the line says so**: the times are data-dependent and unmodelled, which is a named `PROVISIONAL` above |

The last row is the only one not met, and it is the only one whose own text
explains why. That is the difference between a deferral and an omission, and it
is what the first three audits were looking for. The integer core decodes and executes the whole opcode map;
exceptions, traps, interrupt priority and both fault frame formats build and
return; the MMU translates, walks, caches and reports; the caches price
themselves through the bus state machine; and instruction execution time is
composed from the published figures, with `FINDINGS.md` C9's row closing at 6
warm and 7 cold.

**Nothing in the step is unimplemented.** The suite keeps one
unimplemented-instruction placeholder as a live test of the distinction between
"the machine does this" and "we have not finished"; that role has passed from
`BKPT` to `CAS2` to an undefined MMU extension class, each of the first two
having been implemented in turn.

**Four `PROVISIONAL` figures remain, and they are landings rather than gaps** --
`CLAUDE.md`'s "deliberate approximations are fine, documented, with reason and
cost to close". Each is in the PROVISIONAL table below with the measurement or
document that would close it: the ATC's victim choice among clear-history
entries, the U-bit's behaviour after a supervisor violation, which of §11.6.1's
two row groups an encoding selects, and the one-clock bound §11.3.3's rounding
leaves on a published difference of 1. Two of the four need real hardware or an
erratum; the manuals, the sibling manuals, the oracle and the web have each been
taken to their end on them.

**One item moved rather than closed.** The bus termination's *arrival clock* is
now Phase 3's, where its own text always said it belonged: it is the arbitration
point's business, not the processor's, and leaving it in Phase 2 made the phase
look incomplete for work that was never Phase 2's.

**Phase 2b (the rest of the CPU family) is under way**, starting with the 68882
because it is the only one of the four on the DN3500's critical path. Seven
pieces land -- the programming model, the three binary real formats, the
coprocessor interface registers, the rounding stage, the arithmetic operations,
the instruction decode and the fitted part on the 68030's F-line path. The
transcendentals remain a documented divergence rather than an omission; see the
PROVISIONAL table below.

**The 68020's own differences from the 68030 are next, and are in.** The part
is expressed as a *derived* feature set hanging off `ap_cpu_t` in
`src/core/model/`, not as a second CPU and not as conditionals scattered
through subsystems -- these are facts about the silicon rather than about the
board it was soldered to, so every 68020 gets them whatever machine names it.

Three differences are real enough to need code rather than a flag:

- **The instruction cache is not the 68030's with a parameter changed.** Both
  hold 256 bytes. `[68020]` §7.1.1 says the 68020's is "a direct-mapped cache of
  **64 long word entries**" -- one long word to a line, indexed by A2-A7 with A1
  choosing the word; the 68030's is sixteen lines of four, indexed by A4-A7.
  One valid bit per long word here against four per line there, which is exactly
  the distinction a burst fill needs and the 68020 has no burst to need it for.
  The tag is A8-A31 **and FC2**, so a supervisor and a user fetch of one address
  occupy different entries while program and data space do not.
  `src/core/cpu/m68020/ap_m68020_cache.c`, `m68020_cache_suite`, 16 tests.
- **There is no data cache at all.** Zero, not small: every operand access goes
  to the bus. Modelling it as a small data cache would make data cheaper than
  the hardware's, silently and everywhere.
- **`CALLM` and `RTM` exist here and nowhere else.** The PRM heads both entries
  "(MC68020)". They share ten bits of opcode -- `0000 0110 11` -- and are told
  apart by a field `CALLM` is forbidden to use: its addressing is restricted to
  control modes, so modes 000 and 001 are free for `RTM`. The descriptor and
  frame layer is in, with the validation that is most of the specification:
  only types `$00` and `$01` are recognised, only options `000` and `100` are,
  and types `$10-$1F` are a *documented disable bit* rather than merely a
  reserved range -- "a means of disabling any module by setting a single bit in
  its descriptor, without loss of any descriptor information".
  `src/core/cpu/m68020/ap_m68020_module.c`, `m68020_module_suite`, 17 tests.

Two facts came from the **page images** of Figures D-1, D-2 and D-3 rather than
extracted text, and one of them would have been wrong otherwise. The extracted
Figure D-2 had lost its leading column, leaving `Register` apparently spanning
bits 13-12; the image shows **D/A at bit 15 and Register at 14-12**, with bits
11-0 zero. The second is a structural fact worth recording: the stack frame's
first *word* is the descriptor's first *long word* shifted right sixteen --
Opt, Type and access level keep their widths (3, 5, 8) and simply lose the
descriptor's reserved half. That is what "copied to the frame from the module
descriptor" means concretely.

What the 68020 item still owes is its **boot verification**, which carries to
the 68851 item: a DN3000 has no on-chip MMU, so there is nothing to boot until
the external PMMU lands. That is a dependency, not a deferral.

**The 68851 has started, with its translation control registers.** The part is
the DN3000's MMU and is also the "external hardware" the 68020 manual defers to
for access-level checking -- §6.1 names `CAL`, `VAL` and `SCC` as the registers
`CALLM` and `RTM` drive, which is the other end of the module-call layer landed
above. The two subsystems are one mechanism split across two chips.

`TC` (§6.1.3, Figure 6-3) carries the whole tree geometry in one equation:
"the TIx fields are added together, and this sum is added to PS and IS. The
total must be 32." Discarded bits, index bits and page offset must account for
a logical address exactly once, or the write raises an MMU configuration
exception -- and the register still takes the value, with only `E` cleared, so
software can read back what it tried. Two further rules make most bit patterns
illegal: page size bit 3 must be one (so 256 bytes is the floor and `PS` is a
logarithm), and a zero `TIx` is a *terminator* rather than a level indexing
nothing. The terminator does not excuse a field from the sum, which is why
software must zero the levels it does not use.

The root pointers (§6.1.1, Figure 6-1) are three registers of one format for
user, supervisor and DMA -- the DMA tree being the thing the 68030 has no
equivalent for. The limit field's `L/U` bit reverses the sense of the
comparison rather than selecting a second field, and the manual gives *two*
ways to switch the check off (`$7FFF` with `L/U` clear, `$8000` with it set);
both are modelled, because recognising only the first would enforce a lower
bound of zero and be harmlessly right for the wrong reason. One interaction is
worth its own note: `FCL` suppresses the limit check, except for a `DT = $1`
page descriptor, where it runs "regardless of the state of the FCL bit" -- the
case that most needs it, since a page descriptor walks no table and the limit is
the only bound on the direct mapping it creates.

Both figures were read from the page images. That caught a defect the extracted
text would not have: `TC`'s implemented-bit mask, written from the prose,
claimed bit 30, and the figure shows bits 30-26 as a single run of
unimplemented zeros between `E` and `SRE`.

**The six descriptor formats are in, and the interesting fact about them is
that a descriptor does not know what it is.** §5.1.5.2.1: "the exact
interpretation of the bits in a descriptor is determined by three factors: the
value of the DT field of the descriptor, the state of the table search, and the
value of the DT field of the **previous** descriptor used in the search."

Two things therefore arrive from outside the descriptor, and modelling either
one from the descriptor alone produces a core that walks tables plausibly and
wrongly:

- **Its width.** `DT = $2` in the *previous* descriptor makes this one four
  bytes and `$3` makes it eight. Read at the wrong width it is not a descriptor
  with one bad field -- it is a misaligned read of the entire table.
- **Its type.** `DT = $2` is a *table* descriptor while table index fields
  remain and an *indirect* descriptor once they are exhausted. Identical bits,
  decided by how far the search has got. Figure 5-10 is transcribed in full,
  including its two "illegal" cells -- an indirect descriptor naming another
  indirect descriptor -- which "are treated as the 'invalid' type by the
  MC68851", so a chain terminates instead of looping.

The type-1/type-2 split follows from the same idea: a type-2 page descriptor
arises when the search ends early, so there are still levels beneath it for a
limit to bound, and it carries one; a type-1 arises when the indices are spent
and there is nothing left to bound, so it does not. In short format the two are
byte-for-byte identical, which is why one decoder serves both and takes no type
argument.

Three alignments fall out of what each descriptor points at: a table address is
16-byte aligned, a page frame 256-byte (the smallest supported page), and an
indirect descriptor's target 4-byte. And every *long* format puts `DT` in the
upper long word at bits 33-32 where every short format puts it at bits 1-0 --
an asymmetry a reader of the figures is likely to smooth over, so it has its own
test.

**The status and protection registers close the seam with the 68020.** §6.1 is
explicit: "the MC68020 instructions CALLM and RTM can read and alter CAL and
VAL under control of the MC68851 access level protection mechanism." The
external hardware the 68020 manual defers to *is* this part, so
`ap_m68020_module.c` and `ap_m68851_regs.c` are two ends of one mechanism. `AC`
even carries `MDS`, which fixes the boundary a 68020 module descriptor may fall
on -- a rule about the CPU's data structure, enforced by the MMU.

Three encodings are worth naming because the obvious reading of each is wrong:

- **`SCC` is a range test over a bitmap, not a comparison.** "If the current
  access level is n and the MC68020 requests a call to a module of privilege m
  where m < n, the MC68851 will instruct the CPU to change stack pointers if
  **any** bit of SCC between n and m (inclusive) is set." A bit at neither
  endpoint still forces the change -- calling from level 5 to level 1 with only
  level 3 set changes the stack, because the call crosses level 3. Reading it as
  "check the destination's bit" would skip exactly the calls the intermediate
  bits exist to catch.
- **`ALC = $0` disables access checking**, rather than selecting one level. The
  field counts *address bits*, so the level count is two raised to it, and the
  eight-level ceiling is why `CAL` and `VAL` implement only three bits.
- **`MDS = $0` makes every module descriptor invalid**, rather than accepting
  any alignment. No address satisfies it -- which is how the mechanism is
  switched off, and is the opposite of what "no alignment requirement" would
  mean.

`CAL` and `VAL` hold their three-bit level in the *upper* bits of an eight-bit
register, so it lines up with the high-order logical address field it is
compared against instead of needing a shift.

**The ATC is in.** It is 64 fully-associative entries, and the first modelling
decision was to *not* invent a bit layout: Figures 5-21 and 5-22 draw named
fields with no bit numbers, because "the information contained in the ATC is not
directly accessible to the programmer". A packed word would make the state hash
depend on a choice the hardware never made, so the entry is a struct of fields.

A match needs three things, and the third is an escape hatch: the logical
address above the page offset, the function code *exactly*, and either the task
alias matching or the entry's `SG` bit set. The task alias is what lets several
tasks' entries be resident at once; `SG` is what lets one entry serve all of
them, which is the performance reason a root pointer carries the bit. The offset
excluded is the *current* page size rather than one stored in the entry -- so
the same entry covers more or less ground as `TC` changes, which is why writing
`TC` flushes the cache.

Two behaviours are worth stating because a reasonable model gets them backwards:

- **The ATC caches denials, not just translations.** "If access is to be denied,
  an ATC entry is made with the B bit set." The entry *matches* -- it has to, or
  the denial would never be found -- and it is `B` rather than the absence of an
  entry that refuses the access. Protection is evaluated once when the entry is
  made rather than stored and re-checked, which is why `RAL` and `S` are not in
  the entry at all.
- **The lock ceiling is 63 of 64.** "It will not be a copy of the page
  descriptor L bit if there are already 63 entries with set L bits" -- so one
  entry always stays replaceable and the cache cannot deadlock against its own
  locks. A fill that asks for a lock at the ceiling silently gets none; no
  fault, just a cleared bit. That is the same condition `PCSR`'s `LW` reports.

Replacement is invalid-first, then pseudo-LRU among the unlocked. The history
bit is a single generation rather than an ordering, so when every unlocked entry
is marked used the cache starts a new generation instead of refusing -- which is
exactly what makes it *pseudo*-LRU.

**The table search is in, transcribed from Figure 5-23.** That figure is not an
illustration of prose stated elsewhere -- it is the only complete statement of
the algorithm, and several of its rules appear nowhere in the text. The
implementation follows the flowchart's own variable names (`x`, `y`, `SIZE`,
`LAST_SIZE`) so the two can be read against each other.

`LAST_SIZE` is the piece that looks unmotivated until Figure 5-26 explains it:
**the limit check is skipped outright when `LAST_SIZE = 4`.** A short-format
descriptor has no limit field, so whether level B is bounded is decided by the
*format of the descriptor found at level A*. It starts at 8 because a root
pointer is always 64 bits and always carries a limit. The root pointer's own
check is additionally skipped on "FCL = 1 OR DRP IS RP" -- a DMA search always
performs a function code lookup, so its first index is a function code rather
than part of the logical address.

The root pointer selection truth table has eight rows and reduces to two rules:
`FC3` alone sends a non-CPU bus master to the DRP whatever else is set, and the
SRP is reached only when a supervisor access meets an `SRE` that enables it.

Two results confirm earlier work from the other direction. A page descriptor
found with levels still below it makes the flowchart advance `x` and ask whether
the next `TIx` is zero -- if not, levels were skipped and the type is `EARLY`,
which is precisely the type-2 case §5.1.5.2.2 describes. And after following an
indirection the flowchart accepts only `DT = 'PAGE DESCRIPTOR'`, making
everything else invalid: Figure 5-10's two "illegal" cells, seen from the
algorithm's side.

Write protection accumulates down the tree rather than being copied from the
leaf -- §5.2.1.2 calls the cached copy "the effective write protection
determined during the translation table search" -- so a protected table protects
everything beneath it and a clear bit lower down cannot undo it.

The module walks and decides; it does not touch the bus. Descriptor fetches go
through a caller-supplied function, which is what let the whole algorithm be
tested against real translation trees built in an array, and what will let the
cycle-stepped core drive it one bus cycle at a time without this logic
changing.

**Instruction decode covers `PLOAD`, `PFLUSH`, `PVALID`, `PMOVE` and `PTEST`.**

The finding worth recording is a mistake this work made and then corrected.
The command word's top three bits are an opclass, and **opclass `001` carries
four different instructions**:

```
001 | 000 | R/W | 0000 | FC      PLOAD
001 | 001 | 0   | mask | FC      PFLUSHA
001 | 010 | 0000000000           PVALID  (against VAL)
001 | 011 | 0000000 | Reg        PVALID  (against An)
001 | 100..111 | 0 | mask | FC   PFLUSH / PFLUSHS
```

All eight mode values are used. `PFLUSH`'s own page lists five of them, and an
earlier commit -- having read only that page -- concluded the other three were
undefined. They are `PLOAD` and the two `PVALID` forms. The rule in `CLAUDE.md`
about exhausting the sibling documents has an instruction-level corollary: one
instruction's page is not the encoding.

Opclass `011` holds two `PMOVE` formats told apart by `PReg` (`000`/`001` are
the status registers, `100`/`101` the breakpoint registers), and the three bits
below the opclass are a register number under `PMOVE` and a *level* under
`PTEST` -- the same position meaning different things by opclass alone.

The
five-bit function code specification field is a *prefix code*, and that is the
trap in it: `00000` is the SFC form while `01000` is data register 0, so a
decoder that tested the top bit and then the next would get SFC and DFC right by
accident and register `R0` wrong. Three of its four encodings name something
outside the MMU -- a CPU data register, `SFC`, or `DFC` -- which is why it is a
*specification* rather than a value, and why resolving one needs the coprocessor
interface. One consequence is recorded because it changes what an instruction
can do: "since the SFC of the MC68020 has only three implemented bits, only
function codes $0 through $7 can be specified in this manner", so only the
immediate form can name a DMA function code and only it can flush DMA entries.

`PFLUSH`'s mask makes a flush name a *set* of function codes -- "(ATC function
code bits and <mask>) = (<fc> and <mask>)" -- so a zero mask flushes every
function code and an all-ones mask exactly one. A flush-all is *required* to
carry a zero mask and a zero function code, because a flush-all that names a
function code contradicts itself: the manual forbids the encoding rather than
ignoring the fields.

`PMOVE`'s writes have side effects the register modules will need: a `CRP`
write searches the root pointer table and, on a miss, replaces an entry and
invalidates every ATC entry associated with the replaced one; `SRP` and `DRP`
writes invalidate every entry formed with them **"even globally shared"** --
which is the one place a shared entry does not survive. And only `CRP`, `SRP`
and `DRP` are 64 bits, which is why Appendix A footnotes that a register-direct
`PMOVE` cannot carry them.

The operation word's type field is the same six-type encoding the 68882 uses --
general, conditional, branch word, branch long, save, restore -- which is what
"instruction extensions to M68000 Family processors using the M68000 Family
coprocessor interface" means concretely, and why one F-line decoder serves both
parts by cpID alone. `PBcc`'s displacement size is the low bit of that field, so
the two sizes are simply two types and `PBcc.L` needs no extension word to
declare itself.

The sixteen conditions are `2k + (clear ? 1 : 0)` over B, L, S, A, W, I, G, C --
and the interesting part is what is missing. `PSR` defines nine bits and only
eight are testable: **`M`, the modified bit, has no condition.** The encodings
run contiguously from zero, so there is no gap where an `M` pair could sit. It
is the one `PSR` bit reporting a property of a page rather than the outcome of a
test, and a program wanting it uses `PTEST` and reads the register. The
consequence for the model is that the condition-to-`PSR`-bit mapping is not a
single shift: `G` is bit 8 and `C` is bit 7, because `M` sits between them at
bit 9 and is stepped over.

`PSAVE` produces one of three state frames -- 36 bytes idle, 44 mid-coprocessor,
76 with breakpoints enabled -- and the lengths differ precisely so that the
length identifies the frame. The breakpoint frame subsumes rather than
complements the others: "a coprocessor or module call operation may or may not
have been in progress".

Also worth recording from §6.1.8: **`PSR`'s bit order is deliberate.** "The bits
of the PSR are ordered to allow use of the MC68020 'bit field find first one'
(BFFO) instruction to determine the cause of a fault", with the manual giving
the dispatch sequence. So `B`, `L`, `S`, `A`, `W`, `I` at the top of the
register are in fault-priority order, not arbitrary -- which is a constraint on
any future rewrite of that struct.

**The coprocessor interface is in, and its finding is a comparison.** The 68851
and the 68882 sit on the same M68000 coprocessor interface at cpID 0 and 1, and
Table 9-2's footnote marks which registers each leaves unimplemented. They are
not the same two:

| CIR | 68851 | 68882 |
| --- | --- | --- |
| `$08` operation word | unimplemented | unimplemented |
| `$18` instruction address | unimplemented | implemented |
| `$1C` operand address | implemented | unimplemented |

The reason is concurrency. The instruction address CIR "is used to support
concurrent processor/coprocessor instruction execution and is not implemented by
the MC68851. Primitives returned by the MC68851 do not have the PC bit set" --
the MMU never runs concurrently, so it never has to say which instruction an
exception belongs to. The operand address CIR exists here because `PFLUSH`,
`PLOAD`, `PTEST` and `PVALID` each evaluate an effective address the MMU then
uses, which no floating-point instruction does. A single shared CIR table would
be wrong in both directions, and a test checks the two parts' tables against
each other rather than asserting each alone.

One behaviour is the opposite of the intuitive arrangement: **the register that
is implemented is the one that can fault.** Both unimplemented CIRs are
explicitly exempt -- "accessing this register will not cause a protocol
violation" -- while the operand address CIR raises one on any write outside its
primitive, ignores the cycle and aborts the instruction.

Table 9-3's null primitives come to three of thirty-two encodings, and two of
the three coincide: the idle form and a *false* condition result are the same
bits, told apart by whether the read follows a write to the condition CIR. A
classifier working from bits alone cannot separate them and the model does not
pretend to. Table 9-6's five vectors split pre-instruction (F-line 11, protocol
violation 13) from post-instruction (configuration error 56, illegal operation
57, access violation 58), which decides the stack frame and the resume point;
the table prints both decimal numbers and hex offsets, so the two columns check
each other.

**The parts are fitted into one device.** `ap_m68851_translate()` is the whole
read path: match the ATC, and on a miss run the table search and install what it
found -- including a denial, cached with `B` set, so a second access to a
restricted page costs no descriptor reads. A test asserts exactly that, since
the fetch counter makes it observable.

Three behaviours only appear once the parts compose:

- **A disabled MMU is not a transparent one that caches.** With `E` clear
  "logical addresses are routed directly from the logical address bus to the
  physical address bus" -- no table is walked *and no ATC entry is made*, so
  enabling translation later finds an empty cache rather than a set of identity
  mappings.
- **A rejected `TC` write still writes.** An inconsistent geometry raises the
  configuration error and "the TC register is updated with the data except that
  the E bit is cleared", which is how software reads back what it tried.
- **`PMOVE` to `SRP` or `DRP` invalidates entries "even globally shared".**
  This is the one place a shared entry does not survive, so it cannot reuse the
  ordinary flush -- doing so would leave stale supervisor mappings behind. The
  test pairs it against a `PFLUSH` that *does* spare shared entries.

One approximation is recorded in the PROVISIONAL table: §5.3's root pointer
table is not implemented, so a `CRP` write flushes the current task's entries
rather than replacing an alias. It is a performance mechanism -- no translation
returns a different address, only the hit rate differs.

**`PTEST`, `PLOAD` and `PVALID` execute.** Three readings were worth pinning
down:

- **`PTEST`'s level is a ceiling, and level zero is a different operation.**
  §6.1.8 says throughout "for the PTEST instruction with a level specification
  of zero" the bits report what the *ATC* held, with several "always clear"
  because nothing was walked. Treating level zero as a zero-deep search would
  report a fault where the hardware reports a cache miss. For levels one to
  seven the search stops at the ceiling, and stopping because the instruction
  asked has disproved nothing -- so `I` stays clear, or every shallow `PTEST`
  would look like a missing translation. The search gained a
  `SEARCH_TYPE_TRUNCATED` result for exactly this.
- **`PLOAD`'s direction bit is not a hint.** "PLOADR causes U bits ... to be
  updated as if a read access had taken place. PLOADW causes U and M bits ... as
  if a write access had taken place", so `PLOADW` marks its entry modified and
  a later write through it finds a different state.
- **`PVALID` traps when the operand is *less* than `VAL`.** Lower means more
  privileged, so the instruction refuses a pointer more privileged than the
  caller -- it is the confused-deputy guard, stopping a routine handing on a
  pointer it could not itself have made. The comparison is strict: an equal
  level passes.

**The breakpoint registers close the mechanism whose CPU half landed in Phase
2.** The 68020's `BKPT` runs an acknowledge cycle and this part answers it: with
the enable set and a non-zero skip count it "returns the corresponding
replacement opcode and asserts DSACKx" and decrements, and with the enable clear
*or* the count exhausted it asserts bus error and the CPU takes an illegal
instruction. §8.1 names both routes to that one outcome, so a disabled
breakpoint and a spent one are indistinguishable to the CPU -- and the model
does not distinguish them either.

One detail earns its own test: "the BPE bit is cleared at reset; the skip count
field is not". A reset that cleared the counts would silently rearm every
breakpoint to fire on its first pass, which is the opposite of what a debugger
holding a partly-consumed count expects.

**The CPU family now changes behaviour rather than merely being described.**
Until this point `ap_cpu_features()` was read only by its own tests -- the
features were declared and nothing consulted them, which is a table pretending
to be a model. `ap_cpu_decode()` closes that: it asks the shared decoder and
upgrades `$06C0`-`$06FF` to a module call only where `has_module_calls` says so.

The flag is a **bool defaulting false** rather than an `ap_cpu_t`, so a
zero-initialised CPU is still a 68030 and no existing caller had to change. That
is the conservative default in the right direction: a machine built without
consulting the model behaves as the reference superset, not as an arbitrary
family member.

It is a wrapper rather than a second decoder or a parameterised one, and both
alternatives were rejected for a reason. Copying the 68030's table would leave
two to keep in step, and the one no booting machine exercises would drift.
Threading a family argument through `ap_m68030_decode()` would put 68020
variance inside the 68030's module, which `CLAUDE.md` forbids. The wrapper only
ever upgrades *illegal* to a module call, so it cannot claim a word the shared
decoder already understands.

A sweep of all 65536 opcodes asserts the shape of the difference: the families
disagree on exactly 44 words -- 16 `RTM` forms and 28 legal `CALLM` ones -- and
every disagreement is the 68020 accepting what the 68030 refuses. The subset
relation runs one way, which is what "68020 subset" has to mean if it means
anything.

**And what the 68030 refuses, it refuses by taking vector 4.** `[030]` §8.1.5
(p. 8-9) is unconditional: a first word that is not a valid MC68030 instruction
raises the illegal instruction exception. This core previously reported a status
and *stopped* for such a word -- correct as a verdict, wrong as a machine, since
the hardware enters a handler and Domain/OS relies on it. `$4AFC`, the
deliberate `ILLEGAL` instruction, had always vectored; a word the decoder merely
rejected had not.

The fix is deliberately narrow, and the narrowness is the point. This decoder is
not the 68030's, so a word it rejects may be an instruction not yet implemented
here; vectoring on all of them would dress every unfinished corner up as a
correctly-refusing machine, failing *silently* where stopping fails at the gap.
The trap is therefore taken only where the word is positively identified as
another family member's instruction that this model removed. `CALLM`/`RTM` is
that case and, on this machine, the only one. Everything else still stops.

Vector 4 stacks the faulting instruction rather than the following one, so both
stacked addresses are the PC as it stands -- which is what allows the exception
to be raised before any instruction length is known.

Verified by `test_a_module_call_on_a_68030_stacks_the_faulting_instruction`,
which also pins the limit: `$003D` (`ORI.B` with effective address mode 111
register 101, a field mode 111 has never assigned) must still stop rather than
vector. That guard was first written with `$FFFF` and failed -- `$FFFF` is
F-line, which vectors to the line 1111 emulator quite correctly. Detail and the
oracle consequence in `FINDINGS.md` C89.

### The DN3000 boot moves to Phase 4

Phase 2b's 68020 and 68851 items both carried "`dn3000` boots" as their
verification, and that was **out of order with this plan** rather than blocked
by anything in those items. A boot needs a board; `board/ap_board.c` is the
DN3500's and Phase 3 is the phase whose subject is the core board; Phase 4 is
titled "Storage, then a first boot". So the verification has moved to Phase 4,
where a DN3000 board is now its own item.

The reference is in hand, so this is a scheduling correction and not a gap:
`008778-03` Table 2-6 gives the DS3000's 16 MB physical map, the counterpart to
Table 2-8 that the DN3500 board is already built from. Two differences are
structural -- main memory starts at `100000` rather than `1000000`, and the
Series 3000 has no address translation map, so its DMA reaches physical memory
directly rather than through the map at `017000`.

Nothing in the 68020 or 68851 work blocks that boot, and nothing in it was left
undone for want of one.

## Phase 2b: the 68040 has started

**Its MMU descriptors are in, and it is a different MMU rather than a wider
one.** Three differences shape the module, and each removes something that
complicated the 68851:

- **Every descriptor is 32 bits.** There is no long/short format, so nothing in
  a table search depends on the *previous* descriptor's width -- the single fact
  that most complicated the 68851's walk.
- **The tree is fixed at three levels** (root, pointer, page) rather than
  configurable through four table index fields.
- **Page size varies the address field width, not the layout.** A pointer table
  descriptor's address is bits 31-8 at 4K and 31-7 at 8K, because an 8K page
  table holds half as many descriptors. The page descriptor goes the other way,
  31-12 at 4K and 31-13 at 8K -- and the bit the narrower address gives up
  becomes a *second* `UR` bit rather than being reserved.

The trap is that the two type fields free **different** bits. `UDT` reads "00 or
01 = Invalid ... 10 or 11 = Resident", so its low bit is spare. `PDT` reads "01
or 11 = Resident", so its *high* bit is spare -- but only there, because `00` is
invalid and `10` is indirect, which are entirely different. A decoder that
masked the same bit in both fields would turn every indirect page descriptor
into an invalid one and silently lose a level of the tree.

`CM` is four cache policies where the earlier parts had one inhibit bit, and the
distinction is real: write-through and copyback are *both* cachable and differ
in when a store reaches memory, which a `CI` bit cannot express.

One state is named but not enforced: "page descriptors must not have an encoding
of U-bit = 0, M-bit = 1 and PDT field = 01 or 11 ... the processor's table
search algorithm never leaves a descriptor in this state." It is reachable only
by an operating system writing it directly and the manual gives no defined
behaviour for it, so `ap_m68040_page_descriptor_is_incoherent()` is a query
rather than a fault -- there is nothing to implement, only something worth being
able to name.

**The MMU registers are in, and three of their rules contradict the 68851.**
Writing the third MMU in this project means the danger is carrying the second
one's assumptions into it, so each contradiction has its own test:

- **Writing `TCR` does not flush the ATCs.** "The operating system must flush
  the ATCs before enabling address translation since the TCR accesses and reset
  do not flush the ATCs." The 68851 flushes its whole ATC on any `TC` write with
  the enable clear. A model that helpfully flushed here would hide exactly the
  bug that warning exists for.
- **`PFLUSH` works with translation disabled.** "The MMU instruction, PFLUSH,
  can be executed successfully despite the state of the E-bit", where the 68851
  terminates `PTEST`, `PLOAD` and the module calls with an exception when its
  `E` is clear.
- **Reset does not clear the page size.** "A reset operation does not affect
  this bit. The bit must be initialized after a reset", while `E` *is* cleared
  -- the same shape as the 68851's breakpoint skip count surviving reset, and
  the same trap for a model that zeroes a struct.

Two encodings read against intuition. A `TTR`'s **mask widens** the block it
names rather than narrowing it -- "setting a bit in this field causes the
corresponding bit in the Logical Address Base field to be ignored ... blocks of
memory larger than 16 Mbytes can be transparently translated by setting some of
the logical address mask bits to ones". And the `S` field is one meaning in two
encodings: `00` user, `01` supervisor, `1X` either, so the low bit is a
don't-care *only* when the high bit is set.

In `MMUSR`, two bits are whole answers rather than flags among flags: "if the
B-bit is set, all other bits are zero", and a `T` hit sets `R` "and all other
bits are zero". Both are therefore constructed rather than assembled field by
field, so a caller cannot report a physical address alongside a transfer error
that prevented one being found.

Figure 3-6's glyph between `M` and `W` is a reserved **zero**, not a field named
`O`: the manual's field list runs in descending bit order -- B, G, U1, U0, S,
CM, M, W, T, R -- and skips bit 3 entirely.

**The caches are in, and this is the third organisation the core models.**

| | size | arrangement | tag |
| --- | --- | --- | --- |
| 68020 | 256 B | 64 entries of one long word, direct mapped | logical + FC2 |
| 68030 | 256 B each | 16 lines of four long words, direct mapped | logical |
| 68040 | 4 KB each | 64 sets x 4 ways x four long words | **physical** |

The physical tag is the change that matters beyond size: an earlier part caches
by logical address and can alias across a context switch, while the 68040 caches
what the MMU produced, so its lines survive a switch and its snoop logic can
compare against bus addresses directly.

Two details would be invisible if modelled loosely:

- **A dirty bit per long word, not per line.** "Four additional bits to indicate
  dirty status for each long word in the line", and "only the data cache
  supports dirty cache lines". A copyback of a partly-written line writes back
  only what changed; one bit per line would write back clean data -- identical
  in memory contents and wrong in the bus traffic a probe measures.
- **"Pseudo-random" replacement is fully deterministic.** "Each cache contains a
  2-bit counter, which is incremented for each access to the cache ... the line
  pointed to by the current counter" is replaced. One counter per *cache*, not
  per set, so activity in one set moves the victim chosen in another. Motorola's
  name for it is misleading and the behaviour is exactly reproducible, which is
  what a reference core needs. An invalid line is always preferred, so the
  counter only matters once a set is full.

And a third reset trap in this part, after the `TCR` page size and the ATCs:
"both caches should be explicitly cleared after a hardware reset of the
processor since reset does not invalidate the cache lines."

The tests caught two of my own errors, both in constants rather than code: a tag
that lost a hex digit, and a pair of addresses I had assumed were in different
sets when the index is bits 9-4 and they differ only above it.

**The two ATCs are in, and the manual contradicts itself about the tag width.**

§3.3's `Logical Address` field definition reads -- in the page image, not merely
in an extraction -- "This **13-bit** field contains the most significant logical
address bits for this entry. All **16** bits of this field are used in the
comparison ... when the page size is 4 Kbytes." Both numbers are in one
sentence and they cannot both be right.

Following the resolution order: the `MC68040 Designer's Handbook` on disk does
not describe the field, and a web search surfaced no transcription or erratum
that settles it. So it is derived from numbers the same manual states:

- 64 entries, four-way set associative, so **16 sets** -- and Figure 3-20 draws
  them as `SET 0` through `SET 15`, an independent confirmation. Sixteen sets
  need four select bits.
- At 4 Kbytes the page number is address bits 31-12, twenty bits.
- Twenty less four leaves **sixteen** for the tag.

The 8-Kbyte case checks it from the other side: nineteen page-number bits less
four leaves fifteen, and the manual says "for 8-Kbyte pages, the least
significant bit of this field is ignored" -- sixteen stored, fifteen compared.
So sixteen is the width and "13-bit" is an error in the manual. The test states
the derivation rather than the constant, so it would fail if the geometry ever
changed under it.

Two structural differences from the 68851 matter more than the associativity.
The tag carries **`FC2` alone**, not the whole function code -- the 68040 has a
separate ATC for instructions and data, so it never needs to tell the spaces
apart in a tag. And there is **no task alias**: where the 68851 keeps several
tasks resident by tagging them, the 68040 flushes on a context switch and keeps
only *global* entries. `G` overrides a nonglobal flush "even when all other
selection criteria are satisfied", which makes it a veto rather than one more
criterion.

One polarity is inverted and worth its own test: the 68040's `R` is set when a
search *succeeded*, where the 68851's `B` is set when one *failed*. An entry
copied across without inverting would turn every good translation into a bus
error.

**The table search is in, and the manual states its geometry twice.** Figure
3-8 gives the field widths -- `RI` bits 31-25, `PI` bits 24-18, `PGI` bits 17-12
at 4K or 17-13 at 8K -- and §3.2.1 restates the same shape as concatenation
identities: the `PI` field "multiplied by 4 ... concatenated with the fetched
root-level descriptor's **upper 23 bits**", and at 8K the `PGI` field with "the
**upper 25 bits**". Each identity comes to 32 against the address-field widths
transcribed from Figure 3-11, so the two statements confirm each other, and the
tests check them against *each other* rather than each against my reading.

The tree is three levels and fixed: none of the 68851's four `TIx` fields, its
initial shift or its per-level limits exist. What the page size changes is where
`PGI` ends and nothing else about the shape -- one bit moves between the index
and the offset.

Two rules carry over from the 68851 stated from the other side. Protection
accumulates down the tree -- "setting the W-bit in a table descriptor write
protects all pages accessed with that descriptor" -- and an indirection is
followed exactly once, because "this encoding is invalid for a page descriptor
pointed to by an indirect descriptor", so a chain terminates instead of looping.

Like the 68851's, the search walks and decides without touching the bus:
descriptor fetches go through a callback, so the whole algorithm is tested
against real trees built in an array and the cycle-stepped core can later drive
it one bus cycle at a time.

**The integrated FPU's interesting property is what it refuses.** The 68882
executes forty-odd operations in silicon; the 68040 executes a subset and traps
the rest to the `M68040FPSP`. Table 9-10 names them, and the list is worth
reading twice: every transcendental, as expected -- but also `FINT`, `FINTRZ`,
`FGETEXP`, `FGETMAN`, `FSCALE`, `FMOD` and `FREM`, which are *exactly specified*
and which this core already computes bit-exactly for the 68882.

That resolves a tension rather than creating one. The 68882's transcendentals
are a documented divergence because Motorola publishes bounds and no algorithm.
On the 68040 there is no such problem: **refusing these instructions is the
hardware's behaviour**, so a model that computed them would be wrong in a way no
accuracy could fix -- it would skip an exception Domain/OS on a DN5500 supplies
a handler for. The same instruction is a gap on one part and a feature on the
other, which is why this lives in its own module.

`FSQRT` is the instructive survivor: IEEE specifies it exactly, like `FGETEXP`
and `FINT`, and unlike them it stayed in silicon. So "exactly specified" does
not predict which side of the line an operation falls on -- only the table does,
which is why the table is transcribed rather than reasoned about.

### Table 9-10 omits `FLOG2`, and the same manual proves it

The table lists `FLOG10`, `FLOGN` and `FLOGNP1` and not `FLOG2`. Confirmed in
the page image, so not an extraction artefact -- and implausible on its face,
since log base 10 and natural log are log base 2 times a constant, so hardware
holding `FLOG2` would get the other two nearly free.

Appendix E settles it without leaving the document. Table E-2, listing what the
`M68040FPSP` provides, includes `FLOG2` among the transcendentals alongside
`FLOG10`, `FLOGN` and `FLOGNP1`, and *without* the asterisk that marks
instructions the hardware does implement except for special data types. An
instruction the software package provides outright is one the hardware lacks.
So `FLOG2` is unimplemented and Table 9-10 is defective. The resolution order
paid off inside a single document: the sibling section answered what the obvious
table got wrong.

**A third source settles it beyond argument.** `M68000 Family Programmer's
Reference Manual (1992)` Table 5-2, "Indirectly Supported Floating-Point
Instructions", is that manual's own list of what the 68040 does not execute in
hardware -- and it is the 68040 manual's Table 9-10 *plus* `FLOG2`,
twenty-seven entries against twenty-six. Two manuals from different years agree
with each other; the one that disagrees is missing a row. The test enumerates
Table 5-2 in full and asserts the two lists partition every operation the decode
knows, so the sources are checked against each other rather than each against
one reading.

That manual also has nothing further to offer on the transcendentals'
*accuracy*: its §3.5 on computational accuracy covers only the IEEE-specified
operations, and the per-instruction pages give no algorithm. So the 68882's
transcendental `PROVISIONAL` stands -- Motorola publishes bounds and no
algorithm, and the two routes to closing it are still the ones already recorded.

**But re-reading §4.3.2 from the page image turned a prose decision into a
checkable specification** (`ap_m68882_accuracy.h`, `m68882_accuracy_suite`).
Page 4-7 gives three figures the earlier note did not carry, and one fact that
settles the argument.

The worst case is stated twice: "one unit in the last place of double precision
(which is equal to 4096 units in the last place of extended precision)". The
*double* form is the useful one -- these instructions deliver a
double-precision answer in an extended register, and the extra bits extended
precision would buy are noise. Separately, and not previously recorded, "the
typical error bound for these instructions is approximately 64 units in the last
place of extended precision" -- sixty-four times better than the worst case. And
the hardware reason is given: "an ALU with a finite precision of 67 bits", three
guard bits over the 64-bit significand.

The two worst-case figures **disagree**, and both are transcribed as printed. A
double significand is 53 bits and an extended one 64, so one ULP of double is
2^11 = 2048 ULP of extended, not 4096. One reading closes it -- a bound of ±1
ULP spans a window of 2 ULP -- but that is a guess about intent and the
parenthesis reads as an equality. Neither the Programmer's Reference Manual nor
the 68040's Appendix E, both on disk and both searched, restates the conversion.
A test asserts the discrepancy so nobody quietly reconciles the constants.

The typical figure carries the session's clearest OCR trap. The manual's worked
example makes the error "2^6 times the value of the least-significant bit", and
2^6 is the 64. Both `pdftotext` **and the page image at ordinary resolution**
flatten the superscript to "26"; only the arithmetic recovers it. Had the figure
been taken at face value the typical bound would have been recorded as 26.

**And the manual names a concrete divergence, which is what turns the decision
from an argument into a fact.** §4.3.2 closes: "the exponential functions check
for a zero input value, but do not check for exact integer values. Thus, raising
a number to an exact integer value may not produce an exact result (e.g., the
instruction FTENTOX #1,FP0 does not produce an extended precision value of
exactly 10.0), and the INEX2 bit in the FPSR may be set even if an exact result
is produced." Ten to the power of one is not ten. Any implementation computing a
correctly-rounded `FTENTOX` returns exactly 10.0 and is therefore *visibly* not
this part -- not by a rounding mode, but in the value a program reads back.

**And then the specification showed the decision itself was wrong.** §4.3.2
specifies a *bound*, not a result. An implementation that lands inside it
conforms to everything the manual actually says -- and reporting these
unimplemented is therefore the **larger** divergence, not the conservative
choice. Real hardware given an `FSIN` computes a sine; a model that raises an
unimplemented-instruction exception diverges by the whole result and kills a
process that should have had an answer. Being 64 units in the last place from
the part is a smaller error than not being a floating-point unit at all, and any
program that could tell the difference could equally tell two conforming parts
apart. The `FTENTOX #1` fact does not argue against computing them; it argues
that we cannot be bit-identical, which no route makes possible anyway.

So they are being computed, a family at a time, in `ap_m68882_transcendental.c`.

**The exponential family is in** -- `FETOX`, `FETOXM1`, `FTWOTOX`, `FTENTOX` --
at a worst case **under two units in the last place** across curated and random
arguments spanning each function's whole representable range, measured against
expectations computed to 120 decimal digits. That is 32 times inside §4.3.2's
typical bound and three orders of magnitude inside its worst case.

Nothing calls `libm`. The host's `expl` differs between glibc, musl and macOS
and between versions of each, and a reference core whose results depend on which
machine built it cannot have portable goldens -- which CI asserts. Every value
is produced by this core's own extended arithmetic, so a result is a function of
the input and nothing else.

Three things were needed to get inside the bound, and each was a real finding.

**The `ln2` used in the reduction is split at 32 bits** so that `n * ln2_hi` is
exact for every integer the exponent range can produce, which makes the
cancellation in `x - n*ln2` exact and costs the reduction nothing. Without the
split, half the answer's bits are gone before the polynomial starts.

**An exact product is only exact in the constant it was given.** `FTENTOX` first
came out at 3129 units in the last place despite computing `x * log2(10)` as an
unevaluated exact pair. The pair was exact -- verified against Python to the last
digit -- but `c_log2_10` is `log2(10)` *rounded to 64 bits*, and for a product
reaching 16384 the constant's own error dominates. Adding a second constant
holding the residual took it to 1.04. Worth stating because the exactness of the
product actively hides the problem.

**A shift of 64 is not a shift.** `FETOX` returned an *infinity* for `e^0.5`,
because the round-to-nearest-integer helper computed `mantissa >> (63 - e)` and
`e = -1` makes that 64 -- undefined, and the wild integer then went into the
exponent scaling. It affects every argument whose magnitude falls in a band
around `[0.347, 0.693]`, a very common range, and the random sweep missed it
while a curated vector for `e^0.5` caught it immediately. Curated edge cases and
random sweeps are not substitutes for one another.

One trap in the *test generator* is recorded alongside them, because it produced
a confident four-thousand-unit error that looked exactly like an implementation
fault: the expectation must be computed from the argument **after** rounding to
extended, not from the decimal literal. Rounding `7973.123456789012` to 64 bits
moves it half a unit in the last place, and for an exponential that is a
relative change of the same size in the answer.

**The logarithms are in too**, at a worst case under two units in the last
place over arguments from `2^-9000` to `2^9000`. All four reduce `x` to
`m * 2^k` with `m` in `[1/sqrt2, sqrt2)` and evaluate `ln(m)` as
`2 atanh((m-1)/(m+1))` -- a substitution that maps the whole reduced range onto
`|s| <= 0.1716`, leaves only odd powers, and is at its *best* exactly where a
series in `m - 1` would be at its worst, because `m - 1` is exact there by
Sterbenz's rule. `FLOG2` keeps its exponent term an exact integer rather than
dividing at the end, so the logarithm of a power of two is that power exactly
and raises no `INEX2`.

**And landing them found a live bug in `FDIV`.** The quotient was halved
whenever the dividend's significand was the smaller of the two -- roughly half
of all divides. Two significands in `[1,2)` give a quotient in `[0.5,2)`, so
that case needs the exponent to drop by one *and* the long division to run one
bit longer; the code did the first without the second, leaving the leading one
at bit 62 with an already-reduced exponent. `2/3` came out as `1/3`.

Twenty-seven tests in `m68882_arith_suite` missed it, and the reason is worth
recording: every property they check -- commutativity, `x/x = 1`, multiply and
divide inverting each other -- is satisfiable without ever entering that branch.
`x/x` has equal significands. The round trips were built from values that
divided the other way. A property test is only as good as the operands it is
given, and these were chosen to demonstrate the property rather than to reach
the code. Two regression tests now pin the exact quotients and assert that every
finite quotient comes back normalised.

**Three findings from the logarithms themselves.** `FLOGNP1(-1)` raises `DZ` and
returns a **NAN**, where `FLOGN(0)` raises `DZ` and returns a **negative
infinity** -- page 4-58's note 1 against page 4-56's operation table, read from
both page images. The same mathematical pole with two different documented
results, and modelling them alike returns an infinity from an instruction the
manual says returns a NAN. `FLOGNP1`'s direct path is bounded at a *quarter* and
not a half, because what limits it is not where `1 + x` starts rounding but
where the series stops converging: `s = x/(x+2)` reaches `-1/3` at `x = -1/2`,
twice what the coefficients were chosen for, and the truncation error there was
four thousand units in the last place. And a denormal argument has to be
normalised before reduction, since its significand is not in `[1,2)` at all --
and denormals are exactly where a program asks for a logarithm.

**The trigonometric family is in**, at under three units in the last place out
to arguments of `1e18`. All four share one reduction, `x = n(pi/2) + r` with
`|r| <= pi/4`, and one pair of series; the quadrant `n mod 4` then selects which
of `+/-sin` and `+/-cos` each answer is. That sharing is why `FSINCOS` is a
single instruction rather than two, and a test asserts its results are
bit-identical to the separate ones -- a program computing a sine two ways must
not get two answers.

**The reduction is the entire accuracy story here.** `pi/2` is held to about 199
bits in three pieces, and each `n * pi/2_i` is formed as an exact pair, because
the constant's truncation error is multiplied by `n` and `n` is as large as the
argument. A single 64-bit `pi/2` leaves an absolute error near a *radian* at
`n = 2^63` -- not an inaccurate answer but a meaningless one, and one that would
look entirely plausible. Accuracy therefore holds while `n` fits in a 64-bit
significand, to arguments around `1.4e19`, and degrades beyond. That is the
part's own behaviour: the `FSIN` page says "large arguments may lose accuracy
during reduction, and very large arguments (greater than approximately 10^20)
lose all accuracy". The two thresholds are within a factor of ten, so the
degradation is modelled rather than merely tolerated.

`FSINCOS` needed the second destination register, which the decoder had folded
away. Page 4-101 read from the image: bits 9-7 are FPs and take the sine, bits
2-0 are FPc and take the cosine, and "if FPc and FPs specify the same
floating-point data register, the sine result is stored in the register, and the
cosine result is discarded". The cosine is therefore written first and the sine
second, and the order is the specification rather than a convenience -- writing
them the other way round would silently invert the documented tie-break.

None of the three has a divide by zero, which is worth stating because it looks
like it should: `FTAN` at `pi/2` is a large finite number, since `pi/2` is not
representable and the argument never lands exactly on the pole.

`step_suite` had used `FSIN` as its example of an instruction this model does
not implement. It now uses `FMOD` -- a remainder form, outside the
transcendental work -- so the test stops needing an edit every time a family
lands.

**The inverse trigonometric family is in**, at under three units in the last
place. `FATAN` carries the slowest-converging series in this file -- at
`|t| = 1` it would need thousands of terms for extended precision -- so it
reduces three times before evaluating: `pi/2 - atan(1/x)` for arguments above
one, then `pi/4 + atan((t-1)/(t+1))` down to `tan(pi/8)`, then a half-angle
identity down to `0.1989`. Sixteen terms then suffice. Each reduction was chosen
so the correction is small against a constant offset, so none of them can cancel
badly.

**`FACOS` is `2 atan(sqrt((1-x)/(1+x)))` and deliberately not `pi/2 - asin(x)`.**
As `x` approaches one the answer approaches zero, and the subtraction form would
compute it as a difference of two numbers both near `pi/2` -- losing a bit of
the result for every bit `x` is close to one. At `x = 1 - 2^-41` the answer is
about `2^-20`, and a cancelling implementation would leave half the significand
meaningless. The test checks the value against `sqrt(2(1-x))`, the leading term
of the expansion, rather than merely checking it is non-zero.

**Three functions on adjacent pages give three different answers to an infinite
argument**, and all three were read from the manual rather than assumed.
`FATAN(+/-inf)` is `+/-pi/2` and its exception byte reads `OPERR: Cleared` -- it
has no domain error at all, because an infinite *tangent* is an ordinary limit.
`FASIN` and `FACOS` read "set if the source is infinity, > +1 or < -1", so an
infinity *is* an error there. And the forward `FSIN` treats an infinite *angle*
as an error too. Neither `FASIN` nor `FACOS` has a divide by zero: their
endpoints are finite results, and only `FATANH` -- whose poles really are at
`+/-1` -- raises `DZ` there.

One test bug worth recording: the first draft wrote `1 - 2^-41` with the
exponent of one rather than of a half, producing a value near *two*, which is
outside the domain and correctly returned a NAN. The failure looked like an
implementation fault and was a constant written at the wrong exponent.

**The hyperbolic family is in**, at under 3.1 units in the last place, and with
it all nineteen of §4.3.2's transcendentals. Each is written in the form that
does not cancel: `FSINH` as `(u + u/(u+1))/2` with `u = e^x - 1` below one --
where `e^x - e^-x` is a difference of two numbers both near one -- and the
direct difference above one, where it is safe and simpler; `FTANH` as
`u/(u+2)` with `u = e^(2x) - 1`; `FATANH` as `ln1p(2x/(1-x))/2` rather than a
logarithm of a ratio. `FCOSH` needs no such care, both its terms being positive,
and so has one path for the whole range.

**This family carries the one manual defect this work corrects rather than
transcribes.** Page 4-26's description of `FATANH` reads: "the result is equal
to -infinity or +infinity if the source is equal to +1 or -1, respectively".
Read as written, `atanh(+1)` is a *negative* infinity. It cannot be: `atanh` is
odd and strictly increasing on `(-1, +1)`, and `atanh(x) = (1/2)ln((1+x)/(1-x))`
has a numerator that grows and a denominator that vanishes as `x` rises to one.
The same page's exception byte and operation table are both consistent with
`atanh(+1) = +infinity`; only the sentence is transposed.

Fourteen suspect entries have been recorded across this work and thirteen were
transcribed as printed, because proving a figure wrong is not the same as
knowing its value. This one is different: the mathematics does not merely refute
the printed text, it supplies the *unique* replacement -- there is no third way
to assign two signs to two arguments. That is the standing rule's second half
satisfied for the first time, and it is the only reason this one is changed. A
test states the defect and the reasoning so the correction cannot be mistaken
for a transcription error of ours.

Two smaller findings. The three forward hyperbolic functions have `OPERR`
cleared -- every real argument is in range and an infinity is a limit -- but
their limits all differ, and each was read from its own operation table: `sinh`
goes to an infinity keeping the sign, `cosh` to `+inf` for both signs because it
is even, and `tanh` to `+/-1.0` rather than to an infinity at all. And
`FATANH`'s domain check needed both halves of the comparison: a magnitude just
above one has the *same* exponent as one and only a larger significand, so
testing the exponent alone let `1 + 2^-63` through to a computation whose
logarithm argument was negative.

## The 68882 is complete

### The source operand transfer: `FADD.S (A0),FP1` now runs

Until this landed, every 68882 *operation* worked and almost no real
floating-point code did — because a compiler emits far more memory operands than
register-to-register ones, and opclass `010` reported our gap. The gap was never
arithmetic. §4.8.3's R/M bit says only that the operand is external; `[030]`
§10.4.9 says whose job it is to go and get it: "the processor calculates the
effective address using the appropriate effective address extension words at the
current scanPC ... The main processor then transfers the number of bytes
specified in the primitive."

**The division of labour is modelled; the primitive exchange is not.** On
hardware the FPCP answers the command word with an *evaluate effective address
and transfer data* response primitive naming a length and a direction, and the
68030 obliges through the CIRs. Nothing on this machine can observe that
exchange — the CIRs live in CPU space and Domain/OS reaches the part only through
F-line instructions — so the primitives are not encoded. What is modelled is the
*shape* they impose, and it is the reason the interface is two calls rather than
one: `ap_m68882_source_transfer` asks the part what to fetch, the 68030 fetches
it, and `ap_m68882_execute_source` runs the operation on it.

That ordering is forced, not stylistic. **The format decides the address.** A
postincrement steps by the operand's length, so a model that fetched before
asking would have to guess a length, and `(A0)+` would leave the address register
wrong for every format but the guessed one — while still producing the right sum
on the first iteration of a loop, which is how it would survive a casual test.

Three details that are easy to get wrong and are each a test:

- **Extended is twelve bytes, not ten.** The first long word holds the sign and
  exponent and then *sixteen unused bits*; the mantissa is the other two. A
  decoder packing the 80 used bits contiguously reads every mantissa two bytes
  out of place while still getting the sign and exponent right — so the value is
  wrong and nothing faults. The test poisons those bits with `$AAAA`.
- **Integer sources are signed.** §3.1: "The three signed (twos complement)
  integer data formats ... are identical to those supported by the M68000 Family
  architecture." A byte of `$FF` is `-1`. A converter that widened without
  sign-extending would be right for exactly half of every integer operand.
- **An immediate operand is counted in words.** §4.7: "All FPCP instructions are
  from two to eight words in length ... the longest case is for an immediate
  operand of six words - the X or P format." So a byte operand still occupies a
  whole word, and advancing the program counter by one byte instead would leave
  it odd and fault the next fetch.

**Two refusals arrived with it, and both are the machine's trap rather than our
gap** — a distinction worth the tests, because reporting either as F-line or as
unimplemented would look entirely plausible. A data register may not hold an
operand longer than four bytes (`FADD`'s own page: "Only if <fmt> is Byte, Word,
Long, or Single"; §10.4.9 states the same rule as lengths *and* names the
consequence: "all other lengths ... cause the main processor to initiate protocol
violation exception processing"). An address register may not hold one at all —
its row in that table carries dashes instead of an encoding. Each is tested
against a legal neighbour, so what the test pins is the *rule* and not the
addressing mode.

**That made stack frame format `$9` reachable, and the plan had recorded it as
unreachable.** The old reasoning was that the coprocessor mid-instruction frame
exists to resume a *suspended* instruction, and this core's 68882 completes
within the step that issues it — true, and beside the point: Table 8-6 puts
**main-detected protocol violation** in the same row, and the 68030 raises that
one before the coprocessor is involved at all. The frame is ten words, the
six-word frame's two addresses plus four INTERNAL REGISTERS words, which are
written as zero and marked `PROVISIONAL` for the same reason the bus fault frames
make the same approximation: there is no microsequencer state to save. They are
*written* rather than skipped, so a handler cannot read the previous program's
data out from under a documented field name. What it costs is stated where it is
paid — an `RTE` from this frame is still declined, because resuming needs the
dialog those words describe.

Format `$9` also has a **PC rule of its own**: Table 8-6 gives it "[Next word to
be fetched from instruction stream]", not the next instruction. Here the two
coincide, because the violation is detected before any extension word is read,
and `ap_m68030_stacks_next_instruction` records why rather than relying on the
coincidence.

**A latent decode bug surfaced while wiring this.** Table 4-13 tabulates the
command word's low seven bits *as an operation*, which they are only for the two
arithmetic opclasses. For a packed decimal store they are a k-factor, for the
control registers a register select, for `FMOVEM` a register list — and
`FMOVECR` is the same exception from inside opclass `010`, where RX = 7 makes
them a ROM offset. The check had been applied to all of them, so a k-factor
landing in one of Table 4-13's gaps would have raised F-line on an instruction
the hardware executes. It was unreachable before, because nothing had ever
decoded a non-arithmetic opclass far enough to be asked.

Still open on the source side: **packed decimal**, which declines rather than
being decoded as binary — §3.6's decimal-to-binary conversion is separate
arithmetic from everything else in the part, and turning a BCD operand into a
plausible wrong number would hide the gap. The boundary has moved one step rather
than vanished: the *store* direction (opclass `011`) needs the reverse of every
conversion, plus the rounding and exceptions that come with narrowing an extended
value on the way out.

### The store direction: a result reaches memory

Opclass `011` closed the pair, so load-compute-store — what a compiled loop
actually is — now runs end to end. **Storing is not loading read backwards.**
Loading is exact, because every source format fits in extended with room to
spare. Storing *narrows*, and narrowing is where the rounding mode, three
separate special-case tables and the exception byte all arrive at once.

Three rules a symmetric implementation would get wrong, each of which fails
quietly:

- **A store rounds to the destination format, not to the FPCR's precision.**
  §2.2.2: "If the destination is a memory location, the PREC bits are ignored.
  In this case, a number in the extended precision format is taken from the
  source floating-point data register, rounded to the destination format
  precision, and written to memory." A model that consulted PREC — which is
  right there in the register the rounding *mode* comes from — would double-round
  every store made by a program that had set the mode byte. Correct for most
  programs, wrong for the ones that use it, and invisible either way.
- **A store leaves the condition codes alone.** The FMOVE page's Status Register
  section is flat: "Condition Codes: Not affected", "Quotient Byte: Not
  affected". Every arithmetic operation sets them, so a store routed through the
  common result path would silently rewrite the codes an earlier compare had
  left and send the branch after it the wrong way. This is why
  `ap_m68882_execute_store` accrues exceptions but does not go near
  `set_condition_from`.
- **An integer destination clears `OVFL` and `UNFL` entirely.** It reports only
  `OPERR` — "Set if the source operand is infinity, or if the destination size
  is exceeded after conversion and rounding" — plus `INEX2` and `SNAN`. The
  overflow of an integer store is an *operand error*, not an overflow, and the
  saturated result comes with it: "the largest positive or negative integer that
  can fit in the specified destination format size".

**Gradual underflow is implemented rather than approximated.** A value below the
destination's smallest normal becomes a subnormal there, not a zero — and
flushing to zero would have been the invisible failure of the set, since the
number stored is a perfectly ordinary zero that sets nothing a program can
notice. Doing it properly needed the rounding stage generalised from a
*precision* to a *bit count*, because a subnormal's available significand is not
the format's precision: it loses one bit per power of two below the minimum
exponent. Rounding to full precision and then shifting would round twice, which
is a different answer near a tie — the same reason `ap_m68882_round` folds
discarded bits into guard, round and sticky instead of chopping them first.

The two NAN rules are §6.1.2's and differ by destination *kind* rather than
width. For S, D and X: "the SNAN bit in the NAN is set to one and the resulting
non-signaling NAN is transferred to the destination ... although the input NAN is
**truncated** if necessary" — truncated, because a NAN's significand is a payload
and rounding it would carry into neighbouring bits of whatever it encodes. For B,
W and L: "the 8, 16, or 32 most significant bits of the SNAN significand, with
the SNAN bit set". A *quiet* NAN into an integer is an operand error instead,
which Table 6-2 states ("Source is Non-Signaling NAN") and the instruction page
leaves to §4.5.4 — the two agree, and the table is the more specific.

**A live defect in `FINT` and `FINTRZ` fell out of this.** An integer store
reuses `ap_m68882_int` rather than reimplementing mode-following rounding, and
the store's inexactness came back clear on `137.57`. Neither instruction reported
`INEX2`, though both pages list it in the same words: "INEX2: Refer to 6.1.7
Inexact Result". It had gone unnoticed because nothing had asked either
instruction for its exceptions before — the arithmetic suite checked their
*values*, which were right. Fixed with the store, and the store's own test of the
manual's 137.57 example is what pins it.

Extended is the one destination that cannot be inexact: it *is* the internal
format. It still quietens a signalling NAN, because §6.1.2 lists X alongside S
and D.

Packed decimal declines in both directions. §3.6's binary-to-decimal conversion
is separate arithmetic from everything here, and it carries its own operand error
condition — "Result Exponent > 999 (Decimal) or k-Factor > +17" — that belongs
with it.

### FMOVEM: a list, and three rules of its own

`FMOVEM` of the data registers is deliberately **not** the store path with a
loop around it, and the manual is unusually explicit about why. "No conversion
or rounding is performed during this operation, and the FPSR is not affected by
the instruction. This instruction does not cause pending exceptions (other than
protocol violations) to be reported to the main processor." And the consequence,
stated as a note: it "provides the only mechanism for moving a floating-point
data item between the FPCP and memory without performing any data conversions or
affecting the condition code and exception status bits". §6.1.2 agrees from the
other end — `FMOVEM` and `FSAVE` "cannot generate exceptions. Therefore, these
instructions are useful for manipulating SNANs."

So routing it through `ap_m68882_store_encode` would have been wrong in a way
that looks right: a signalling NAN would come back quiet with `SNAN` set, which
is a different value *and* a different FPSR. `ap_m68882_movem_read`/`_write` copy
twelve bytes and touch nothing else.

**The mask's bit order reverses with the addressing mode.** This is the trap the
instruction carries, and the manual prints it as two rows:

    Static, -(An)             --  FP7 FP6 FP5 FP4 FP3 FP2 FP1 FP0
    Static, (An)+ or Control  --  FP0 FP1 FP2 FP3 FP4 FP5 FP6 FP7

One rule unifies them and is the one worth holding: **bit 7 is always the
register transferred first**, and the transfer runs from bit 7 down to bit 0 in
every mode. Predecrement goes FP7 through FP0 down through lower addresses; the
others go FP0 through FP7 up through higher ones. So the walk is one loop, and
`ap_m68882_movem_register` is where the reversal lives entirely.

The manual's own programming note is the proof it matters: a procedure passing a
live-register mask to a callee has to pass *two* of them, "due to the different
transfer order used by the predecrement and postincrement addressing modes" —
which is also why `MODE` has separate dynamic encodings for the two.

The test that pins it is a round trip: store all eight with `-(A0)`, load them
back with `(A0)+`. That uses both tables, and a model using one ordering for
both would reverse all eight registers while still moving the address register
exactly the right distance. The memory layout is asserted alongside it, because
a symmetric mistake would otherwise cancel out.

Two smaller rules. The command word does not decompose the way every other one
does — `11 dr | MODE (2) | 0 0 0 | REGISTER LIST (8)`, where the list crosses the
boundary the general decode draws between `RY` and the extension field and the
mode sits where `RX` does but is two bits wide. And the address register steps
twelve bytes *per register* rather than once for the instruction: before each
store in the predecrement case, after each load in the postincrement one, so a
predecrement store leaves the register pointing at the image it wrote last and a
postincrement load leaves it one byte past the image it read last.

The addressing modes are a category *plus one mode*, differently in each
direction: reading allows the control modes and `(An)+`, writing the control
alterable modes and `-(An)`. No category expresses that, so it is its own check —
"If the effective address is the predecrement mode, only a register to memory
operation is allowed."

### The system control registers, and what makes the FPIAR worth having

Opclasses `100` and `101` are one instruction wearing two names: "if a single
register is selected, the opcode generated is the same as for the FMOVE single
system control register instruction". So `FMOVE.L D0,FPCR` and
`FMOVEM.L (A0),FPCR/FPSR/FPIAR` are the same path here, and the register count
is the only thing that changes.

Three rules, each stated by the manual and none inferable from the data-register
transfers:

- **Unimplemented bits read as zeros and are ignored on the way in.** "A 32-bit
  transfer is always performed, even though the system control register may not
  have 32 implemented bits." FPCR is two bytes -- the enable byte at 15-8, and
  Figure 2-3's mode control byte, which is PREC at 7-6, RND at 5-4 and *zero at
  3-0*. FPSR's condition code byte uses only 27-24, since Figure 2-4 prints
  31-28 as one `0` field, and its accrued exception byte only 7-3. FPIAR is an
  address and has all thirty-two. A model that stored the whole word would hand
  the extra bits back on the next read and contradict itself rather than fault.
- **The order is fixed at FPCR, FPSR, FPIAR** "regardless of the addressing mode
  used" -- no reversal, and no dependence on the mode at all, which is the
  opposite of the data registers a page earlier.
- **The address register steps once**, by four times the register count: "the
  address register is first decremented by the total size of the register images
  to be moved (i.e., 4 times the number of registers) and then the registers are
  transferred starting at the resultant address". So a predecrement here runs
  *upwards* from the decremented base, where the data-register `FMOVEM`'s
  predecrement runs downwards twelve bytes at a time. Two instructions, adjacent
  in the manual, with opposite stepping rules.

Writing the FPSR replaces every bit including the condition codes -- "all bits
are modified to reflect the value of the source operand" -- which is what a
context restore needs and what a merge would get wrong. And no write can raise
anything: "a write to the FPCR exception enable byte or the FPSR exception
status byte cannot generate a new exception, regardless of the value written",
so enabling a trap whose exception bit is already set does not fire it.

**The FPIAR now tracks**, which it had to before the transfer could move
anything but zero. §2.4 gates it twice, and the second gate is the one worth
naming: the register "is loaded with the logical address of an instruction
before the instruction is executed (**unless all arithmetic exceptions are
disabled**)". With the enable byte clear it does not move at all -- a condition
easily dropped, because a register that always records looks perfectly
plausible.

The first gate is what the register is *for*: "Since the FPCP FMOVE to/from the
FPCR, FPSR, or FPIAR and FMOVEM instructions cannot generate floating-point
exceptions, these instructions do not modify the FPIAR. These instructions can
be used to read the FPIAR in the trap handler without changing the previous
value." A model that updated it on every instruction would destroy the value on
the way to reading it.

One judgement is recorded rather than hidden: `BSUN` is **not** counted among
the arithmetic enables that gate the tracking. The manual says *arithmetic*
exceptions, and BSUN is the branch-on-unordered one, raised by a conditional
test rather than by an operation. Reading it the other way would make the
register track slightly more often, and that is the alternative if this turns
out wrong. Nothing observable distinguishes them today, because the conditional
instruction types are not yet executed.

### FMOVECR, and a value the documents cannot settle

`FMOVECR` completes the general type: every general-type *instruction* now
executes, and what remains is a data format rather than an instruction.

It reads the part's own ROM and touches no memory, which is why it lives inside
the memory-to-register opclass with `RX = 7` and needs no effective address at
all. And it **rounds to the FPCR's precision** — the exact mirror of the store
rule, worth holding as a pair: a store to memory ignores the PREC bits because
the destination format decides, while here the destination is a register and
PREC is the whole of it. Only `INEX2` can be raised; the instruction page lists
`OVFL` and `UNFL` as Cleared, so a constant outside the selected precision's
*range* is not an overflow.

**The offsets are published and the values are not.** Neither the part's own
manual nor the `M68000 Family Programmer's Reference Manual` prints a bit
pattern — both print a name: `$00` is "π", `$30` is "1n(2)", `$3F` is 10^4096.
So the resolution order ran out at step two, and the values are computed
independently to 200 decimal digits and correctly rounded, the same route the
transcendentals took.

That leaves one thing open and it is worth being exact about what: the computed
values agree with the canonical 80-bit constants — π is `$4000
C90FDAA22168C235`, ln(2) is `$3FFE B17217F7D1CF79AC` — which is agreement with
something outside this project and a real check. What it is *not* is proof that a
particular 68881 mask set holds those bits. A ROM is not obliged to be correctly
rounded, and this one is not documented either way. **Closing route: instrument
the oracle and read all 22 back**, then classify the difference the way every
other oracle disagreement is classified. It is deliberately not done here,
because the documents were the cheaper source and they answered everything
except this.

The undefined offsets are a **documented absence of a right answer** rather than
a gap in the model: "The values contained at offsets other than those defined
above are reserved for the use of Motorola, and may be different on various mask
sets of the FPCP." There is no value to be correct about. The PRM names the only
convention that exists — "These undefined values yield the value 0.0 in the
M68040FPSP" — and that is what a reserved offset returns, so a program reading
one sees a stated value rather than whatever the register held. The instruction
still executes; it is not an illegal encoding.

### FBcc, and the one instruction that must not clear the exception byte

The branch is its own instruction *type* rather than an opclass, which made it
the one place where the coprocessor's half was already finished and the main
processor's was not: `ap_m68882_condition` had evaluated all 32 predicates since
the register work, and nothing called it.

The mechanics are small — word and long displacements, relative to **the
instruction's address plus two** — and `FNOP` costs nothing extra, since it "uses
the same opcode as the FBcc.W <label> instruction, with cc = F (non-signalling
false) and a displacement of zero".

**Wiring it exposed a live defect.** A conditional does not clear the exception
byte, and it is the only instruction in the part of which that is true. Every
arithmetic page lists SNAN, OPERR, OVFL, UNFL, DZ, INEX2 and INEX1 as "Cleared";
`FBcc`'s lists all seven as "Not Affected", with only "BSUN: Set if the NAN
condition code is set and the condition selected is an IEEE non-aware test". The
accrued byte narrows the same way: "The IOP bit is set if the BSUN bit is set in
the exception byte. **No other bit is affected**."

`ap_m68882_condition` had gone through `apply_exceptions`, which clears the byte
first and then re-accrues. So testing a condition wiped the record of whatever
last raised an exception — on every branch a program takes — and re-accrued an
earlier instruction's bits into the accrued byte a second time. It was
unreachable until now, because nothing executed a conditional.

**Table 4-22's two halves are the opposite way round from the obvious guess**,
and this is worth recording because the FBcc page's wording invites the wrong
one. Bit 4 selects the half. The *low* half, `$00`-`$0F`, is the **IEEE aware**
one — `F`, `EQ`, `OGT`, `OGE`, `UN`, `UEQ`, `NE`, `T`, whose names say what they
do about unordered operands — and it raises nothing. The *high* half is the
non-aware one, and the table gives its entries "Signaling" names for exactly
this reason: `SF`, `SEQ`, `SNE`, `ST`. So `$01` is `EQ` and silent while `$11` is
`SEQ` and signals, and the test states the pair: one NAN, two spellings of one
comparison, two different answers.

### FDBcc, FScc and FTRAPcc, and a defect in Table 4-19

Three instructions, one type (`001`), one command word format, and Table 4-19's
instruction-specific field to tell them apart. §4.7.2 states the division of
labour plainly: "the MPU writes a conditional predicate to the FPCP condition CIR
for evaluation ... The true or false result is returned to the main processor
with the null primitive." So the coprocessor's whole half was already
`ap_m68882_condition`, and decrementing a register, writing a byte and taking a
trap are all the main processor's.

**`FDBcc`'s branch base is a third rule.** "The value of the PC used in the
branch address calculation is the address of the displacement word" — where
`FBcc`, two pages earlier, uses the instruction's address plus two. The predicate
word sitting between them is the whole difference, so a base carried across from
`FBcc` is off by exactly its width. The counter is decremented in its **low
sixteen bits only**, which is right for every count that never borrows and wrong
for the one that does.

`FTRAPcc` discards its optional immediate operand when the condition is false —
discarded, but still *consumed*, or the operand decodes as the next instruction.

**Table 4-19 has a defect at `111 000` and `111 001`.** It marks both
"(Undefined, reserved)", which by its own Note 3 would take an F-line trap. Two
per-instruction statements disagree, and they are the more specific ones:
`FScc`'s own page lists `(xxx).W` at `111 000` and `(xxx).L` at `111 001` in its
addressing mode table, and the `M68000 Family Programmer's Reference Manual` says
of the same instruction "Only data alterable addressing modes can be used" and
lists both. Absolute addressing *is* data alterable.

Two sources against one summary table, so absolute addressing is accepted and
Table 4-19 is recorded as the suspect entry — the same treatment every other
suspect entry in this project gets, transcribed as printed unless something
supplies the unique replacement. Here the replacement is unique: the encodings
are named by two independent tables that agree with each other. The genuinely
reserved rows, `111 101` through `111 111`, do take Note 3's F-line trap, and the
test checks them beside the accepted ones so the reading is a *distinction*
rather than a blanket permission.

### Packed decimal, both directions

This is the last gap in the 68882 and the only one that is a *data format* rather
than an instruction. The references have been read to the end and the design is
no longer an open question; what remains is arithmetic.

**The format**, Figure 3-11 and Table 3-4, ninety-six bits over six words:

| Field | Bits | Meaning |
| --- | --- | --- |
| `SM` | 95 | sign of mantissa |
| `SE` | 94 | sign of exponent |
| `y y` | 93-92 | "used only for +/-infinity or NAN(s); zero otherwise" |
| `EXP2 EXP1 EXP0` | 91-80 | the three exponent digits |
| `(EXP3)` | 79-76 | written on a move *out* only, "if the source operand exceeds the magnitude of a three digit exponent"; a don't care on input |
| `XXXX XXXX` | 75-68 | "don't care bits, which are zero when written and ignored when read" |
| `MANT16` | 67-64 | the integer digit, with the decimal point implicit after it |
| `MANT15`..`MANT0` | 63-0 | the sixteen fraction digits |

Table 3-4's type rows are what distinguish the five data types, and the
distinguishing field is *not* the one the binary formats use: an infinity or NAN
has `SE` and both `y` bits set **and** an exponent of `$FFF`, and infinity is
then told from NAN by the fraction being zero. A zero is an in-range string with
`MANT16` and every fraction digit zero, at any exponent.

**A NAN is copied, not converted.** Note 1: "the fraction part of the NAN is
moved bit-for-bit into the extended precision mantissa ... but no
decimal-to-binary conversion or any other conversion is performed". And the
signalling bit falls exactly where the extended format puts it: the
most-significant bit of `MANT15` becomes the extended integer bit and is a don't
care "as in extended NANs", and the bit below it is the SNAN bit — extended bit
62, the same quiet bit every other path uses.

**Non-decimal digits are not policed.** Note 2: `$A`-`$F` in the exponent of a
*zero* converts to a true zero, but "The FPCP does not detect non-decimal digits
in the exponent, integer, or fraction digits of an in-range decimal string. These
non-decimal digits are converted to binary in the same manner as decimal digits;
however, the result is probably useless, although it is repeatable." Repeatable is
the operative word: this is defined behaviour to reproduce, not an error to
raise.

**Conversion in cannot overflow.** Note 3: "Since in-range numbers cannot
overflow or underflow when converted to extended precision, normalized extended
precision numbers are always produced." The widest string is about
`9.9e999`, comfortably inside extended's `1.19e4932`.

**The rounding rule is a third variant**, and the three now form a set worth
holding together:

- a store to memory rounds to the **destination format**, ignoring `PREC`;
- `FMOVECR` rounds to **`PREC`**, since its destination is a register;
- a decimal *input* rounds to **extended, regardless of `PREC`** — §6.1.8: "the
  result of the decimal-to-binary conversion is rounded to extended precision
  (regardless of FPSR mode byte rounding precision)".

`INEX1` exists solely for this: "the condition that exists when a packed decimal
operand cannot be converted exactly to extended precision in the current rounding
mode", kept separate from `INEX2` so a program can tell a decimal input error
from an arithmetic one.

**The input conversion is implemented, and it needed a bignum.** §6.1.8
specifies *correct rounding*, not a bound — unlike the transcendentals, where a
published interval let an independent algorithm conform. Correct rounding of
`M x 10^E`, with `M` a 17-digit integer and `E` running from -1015 to +999,
needs the exact product, and `5^999` alone is some 2322 bits. An approximation
through the extended multiplier would be off in the last bits in a way no test
could call correct, because the expected values would come from the same
approximation.

So `ap_m68882_packed.c` carries 160 thirty-two-bit limbs — 5120 bits, clear of
the widest intermediate. Limbs are 32 bits so a multiply-accumulate fits a
64-bit product without `unsigned __int128`, a compiler extension this core does
not use, for the same reason the square root writes its own 128-bit arithmetic.

`M x 10^E` is `M x 5^E` scaled by `2^E`, so a positive exponent is a multiply
and a negative one a divide. The division is **bit-serial** rather than
quotient-estimated: only about 65 quotient bits are wanted, so the schoolbook
shift-and-compare is short enough, and it avoids the digit estimation that makes
long division subtly wrong — subtly wrong here would move the last bit, which is
exactly the bit `INEX1` is about.

Two things caught in testing, both worth recording. **The seventeen mantissa
digits start at nibble 7, not nibble 15**: `MANT16` is bits 67-64, the *low*
nibble of byte 3, because bytes 2 and 3 hold the don't-care field above it. The
first version read past the operand entirely. And the conversion's `INEX1` has
to be **merged with the operation's own exceptions** rather than applied
separately, since the exception byte is cleared once per instruction — §6.1.8
puts them in one instruction: "If the instruction is not an FMOVE, the rounded
result is used in the calculation."

Values are checked against expectations computed to 400 decimal digits, the
route the transcendentals took, since neither manual prints a bit pattern for
any of it. `0.1` arriving as `$3FFB CCCCCCCCCCCCCCCD` is agreement with
something outside this project.

**And out again, with the k-factor.** Page 4-67 prints seven conversions of one
value, and that table is the whole specification of what the k-factor does:

| k | 12345.678765 becomes |
| --- | --- |
| -5 | `+1.234567877 E+4` |
| -3 | `+1.2345679 E+4` |
| -1 | `+1.23457 E+4` |
| 0 | `+1.2346 E+4` |
| +1 | `+1. E+4` |
| +3 | `+1.23 E+4` |
| +5 | `+1.2346 E+4` |

The two halves run in *opposite directions*, which is the thing the table says
unambiguously and the prose does not: "-64 to 0 — Indicates the number of
significant digit to the right of the decimal point (Fortran 'F' format). +1 to
+17 — Indicates the number of significant digits in the mantissa (Fortran 'E'
format)." So -5 yields eleven digits and +5 yields five. All seven rows are a
test, reproduced character for character.

"+18 to +63 — Sets the OPERR bit in the FPSR exception byte, **treated as +17**"
is two obligations in one sentence, and the test holds both: the exception is
raised *and* the string is byte-for-byte the one +17 produces. A model doing
either alone would satisfy half of it.

Two more rules from §3.6. A decimal exponent past 999 raises `OPERR` and
"calculates a fourth exponent digit, which is included in the destination
operand" — `EXP3`, at the nibble Figure 3-11 shows and a don't care on the way
in. And inaccuracy going *out* is `INEX2`, not `INEX1`: §3.6 sends it to §6.1.7,
and the two bits are separate precisely so a program can tell the directions
apart.

**One documented absence, recorded rather than approximated.** §3.6 says the part
itself is not correctly rounded on the way out: "the error bounds specified by
the IEEE standard apply only to conversions of values in the range of the double
precision format. The error bound for conversions by the FPCP of extended
precision values which cannot be represented in double precision is
significantly larger. Software must be provided to convert such extended
precision values to decimal." No bound is published for that case, so there is no
figure to model. This core rounds correctly at every magnitude — inside the IEEE
bound where one applies, and better than the part where none does. That is the
same position the transcendentals take, and for the same reason: reproducing an
unpublished looseness is not available, and being wrong in a *documented*
direction is preferable to being wrong in an invented one.



### The 68882 is complete as an implementation, not yet as a verification

Every instruction and every data format executes. The arithmetic is checked
against mathematical truth — expectations generated to 120 and 400 decimal
digits — which for *accuracy* is a stronger statement than the oracle could
make, since this project expects to out-accurate it.

**The audit found something larger than the missing probes: the 68882 was not
reachable from a running machine at all.** `ap_machine_init` never attached one,
so `cpu->fpu` was null on every machine this core builds, and every F-line
instruction took the line 1111 trap — the behaviour of a correctly *unfitted*
machine, which is exactly the trap this core is careful to distinguish from its
own gaps, arriving here for the wrong reason. That is also why no floating-point
probe existed: there was nothing to probe.

The part is now attached and two probes cover it — a ROM constant, an add and a
store conversion in one; both operand directions and an `FMOVEM` of the register
file in the other. Neither perturbed any existing probe line, and debug and
release agree bit for bit.

It is attached **unconditionally**, which is a statement about the harness rather
than about the range: `ap_machine_init` takes no model, so this machine is the
DN3500, the reference superset. A DN3000's absent coprocessor is not expressible
until the machine has a model, and that is a named tail rather than an oversight.

**What is still missing is the oracle comparison.** The 68882's plan item asks for "a probe suite over each
operation and rounding mode; note the oracle's admitted FPU gaps as a divergence
class". `src/core/probe/ap_probe.c` has **no floating-point probe at all**, so
nothing has ever compared this part's behaviour with MAME's inside a running
machine, and the divergence classes have not been drawn.

The determinism golden does not close it and should not be mistaken for it. Its
FNV-1a digest over 38,880 results proves that every build type and platform
produces the same answer — portability, which is a different property from
agreeing with anything outside this project. Both are wanted; only one is done.

So the item is `[~]` rather than `[x]`: the implementation is finished and the
verification it was filed under is not. Recorded this way because the alternative
— leaving it ticked — would make the plan claim a measurement that was never
taken.

### FSAVE and FRESTORE, and the frame this part can never produce

The last 68882 forms, and the only ones that compute nothing: they move the
coprocessor's own internal state, which is why they are instruction *types*
rather than opclasses. Both are privileged.

**A busy frame is deliberately absent, and that is a modelling statement rather
than a gap.** It exists so an instruction suspended part way can be resumed, and
this core's 68882 completes every instruction inside the step that issues it —
nothing can interrupt it half-done, so nothing can generate one. That is the same
reasoning the 68030's stack frame `$9` once carried and lost, so it is worth
being clear about why it holds here and did not there: `$9` was reachable because
a *main processor* rule reached around the coprocessor, the main-detected
protocol violation. No such rule reaches the busy frame — it is produced by the
part, in a state the part never enters.

**Which frame is saved is state, not a constant.** §6.4.2.1: "A save of the null
state results when no FPCP instructions have been executed since the last null
state restore or hardware reset." So the part carries an `executed` flag, set by
*any* instruction and not only an arithmetic one — an `FMOVEM` that filled the
register file is exactly the case where saying "nothing has run" would lose the
programmer's model. A null frame is four bytes and an idle one sixty, so the
frame's length is not fixed and a predecrement steps by whichever was produced.

**The two frames differ in exactly what a context switch cares about.** Restoring
a null frame is "equivalent to a hardware reset of the FPCP. The programmer's
model is set to the reset state, with non-signaling NANs in the floating-point
data registers and zeroes in the FPCR, FPSR and FPIAR." Restoring an idle one
does the opposite: "The programmer's model is not affected by loading this type
of state frame."

**An unrecognised format word is a *format exception*, not a protocol
violation** — "the MPU is instructed to take a format exception" — and the two
refusals are kept apart because reporting one as the other sends a handler
looking for the wrong fault.

One apparent contradiction resolved rather than picked between: Figure 6-5 prints
the null frame's size byte "(UNDEFINED)", FRESTORE's page calls the format word
`$0000`, and the save CIR list gives `$0018`. §6.4.2.1 reconciles all three —
"The size value of a null state frame is not assumed to be valid during a save
operation and is ignored by the FPCP during a restore operation." The *version*
identifies a null frame; the size is a don't care, and version 0 is the wild card
"allowing this state frame type to be restored to a coprocessor of any version".

**The version number is `PROVISIONAL` and there is nothing to transcribe.** "The
version number is an 8-bit value that identifies the microcode version of the
FPCP, and the format of this number is defined internally by the FPCP" — no
manual publishes a value for any part. It is held as state on the part rather
than as a constant, because that is what it is, and because the only behaviour a
program can observe from the documents is self-consistency: what `FSAVE` writes,
`FRESTORE` must accept, and version 0 must be accepted whatever it is. Both hold
for any non-zero choice. Closing route: read it from a real part, or from the
oracle.

The idle frame's CU internal registers, operand register and BIU flags are
written as zeros and marked `PROVISIONAL`, for the reason the 68030's stack
frames give: this model has no microsequencer state to save. Written rather than
skipped, so a handler cannot read the previous program's data from under a
documented field name.

### The transcendentals

All nineteen transcendentals are computed, and the `PROVISIONAL` that stood over
them is closed. The worst error measured anywhere in the family is **under 3.1
units in the last place** of extended precision, against expectations generated
to 120 decimal digits -- twenty times inside §4.3.2's typical bound of 64 and
three orders of magnitude inside its worst case of 4096. Nothing calls `libm`,
so a result is a function of its input and nothing else on every platform and
build type.

They remain *classified* as transcendentals, and that is deliberate rather than
a leftover: §4.3.2's bound still applies, because these are approximations
conforming to a published interval and not exactly-rounded operations like
`FSQRT`. Erasing the marking because the functions now return answers would lose
the only record that they are approximate at all.

The one divergence from the part that is visible rather than hidden in the last
bits stays as recorded: `FTENTOX #1` returns exactly 10.0 here, and §4.3.2 says
the hardware's does not. It is not closable without the algorithm Motorola never
published, and it is in the direction of being more correct than the part.

**An audit against the item's own verification found a real gap, and closing it
found a bug.** The 68882's verification line asks for "a probe suite over each
operation and rounding mode", and the transcendentals had only ever been
measured at round-to-nearest. Checking the other three turned up §6.1.4's
trap-disabled overflow table, which the whole core had been getting wrong:

    RN   Infinity, with the sign of the intermediate result
    RZ   Largest magnitude number, with the sign of the intermediate result
    RM   For positive overflow, largest positive number
         For negative overflow, -infinity
    RP   For positive overflow, +infinity
         For negative overflow, largest negative number

There is one rule underneath -- an infinity when the mode pushes *away* from
zero in that direction, the largest finite number when it pulls back -- and
`finish` had been returning an infinity unconditionally. So `FMUL` under
round-to-zero produced a value the part never produces, and **silently**: `OVFL`
is set either way and only the stored number differs. The same fault was in the
transcendentals' own overflow path. Both now share
`ap_m68882_overflow_result`.

§6.1.4's NOTE turned up a second gap alongside it: overflow is detected against
"the maximum exponent value of the **selected rounding precision**", not
extended's, so a value extended can hold still overflows a single-precision
destination. `2^200` is an ordinary extended number and an overflow at single
precision. The threshold is now precision-dependent, and the substituted finite
value is the largest of *that* precision -- 24 significand bits at exponent 127,
not 64 bits at 16383.

§6.1.5's underflow table is the exact mirror -- a zero when the mode pulls
toward zero in that direction, the smallest denormal when it pushes away -- and
that half was already correct, which is the interesting part. Rounding a tiny
value toward plus infinity naturally produces the smallest denormal *because the
denormal is representable*; an overflowed value is not, so there the exponent
saturates and the documented result has to be substituted. The two halves of one
rule are reached by different routes, and only the overflow half could go wrong
silently. Both are now pinned; neither had been tested, because this suite had
only ever run at round-to-nearest.

**The family has a golden of its own, because the accuracy tests could not
serve as one.** CI asserts that emulated results are identical at `-O0` and
`-O3` and across four platforms, and it does so by comparing each build against
a committed golden -- but the probe golden covers the integer core and contains
no floating-point entry at all, so the nineteen transcendentals sat outside that
guarantee entirely. The accuracy sweeps could not fill the gap: they assert
§4.3.2's *bound*, and a result that moved by one unit in the last place between
build types would satisfy every one of them while meaning the core's output
depended on which flags built it.

So there is one number over the whole family -- an FNV-1a digest of 38,880
results: every function, over 180 arguments spanning exponents from `2^-60` to
`2^60`, at all four rounding modes and all three precisions, with the exception
flags hashed alongside the values so a change in what is *raised* is caught too.
`FSINCOS` is hashed separately, since it returns two results and would otherwise
have its cosine excluded. Every argument is derived arithmetically rather than
drawn from a generator, so the set is part of the source and not a seed.

The two builds were compared before the constant was committed rather than
after: `-O0` and `-O3` produce `0x794C36B690FFECAF` alike. That is the check
being asserted, not an assumption being recorded.

**Denormal arguments found four bugs, and the first was in the core
arithmetic.** Testing every transcendental at the bottom of the exponent range
-- which nothing had done, because the accuracy vectors are all normal numbers
-- turned up wrong answers in nine of the nineteen. Chasing them found the cause
was not in the transcendentals at all.

`ap_m68882_mul` and `ap_m68882_div` denormalised at exponent **zero**, which is
a *legal* extended exponent rather than the first one below the range. §6.1.5's
own footnote settles it: "underflow is NOT detected for intermediate result
exponents that are equal to the extended precision minimum exponent, since the
explicit integer part bit of extended precision permits representation of
normalized numbers with a minimum exponent" -- the same NOTE that makes
`ap_m68882_classify` refuse the single/double rule. Both used `exponent <= 0`
with a shift of `1 - exponent`, so every result at the minimum exponent came
back **halved** and the smallest denormal came back as **zero**. That is `FMUL`
and `FDIV`, not a transcendental fault, and it is silent: a halved denormal is
still a plausible small number.

Three faults in the transcendentals survived that fix. `nx_scale2` flushed a
halved denormal to zero rather than denormalising it, costing `FSINH` and
`FATANH` their whole answer. `FLOGNP1` tested the exponent *field* for its
small-argument path, so a denormal -- whose field is zero -- was excluded from
the very path that exists for small arguments and sent through `1 + x`, where it
vanished. And `FATAN` halves its argument before its series, which destroys the
smallest denormal outright, so it now returns the argument directly below
`2^-40`.

That last threshold had to be derived rather than copied, and copying it was a
fifth bug caught by the accuracy sweep within a minute. `atan(t)` is
`t(1 - t^2/3 + ...)`, so its correction is `t^2/3` and `2^-40` is ample;
`ln(1+x)` is `x(1 - x/2 + ...)`, so its correction is `x/2` *relative* and needs
`2^-64`. Taking the arc tangent's threshold for the logarithm was wrong by
twenty bits -- a million units in the last place at `2^-43`.

The determinism golden earned its place here: it still reads
`0x794C36B690FFECAF` after all five changes, which is how the normal range is
known to be untouched. A fix at the bottom of the exponent range that quietly
moved an ordinary result would otherwise be invisible.

**A NAN argument now comes back with its payload, which it did not before.**
§4.5.4 opens by saying the operation tables carry no NAN row "because NANs are
handled the same way in all operations" -- transcendentals included -- and then:
"if either, but not both, operand of an operation is a NAN, and it is a
non-signaling NAN, then **that NAN is returned as the result**."

`FADD` had this right and all nineteen transcendentals did not: they returned a
fixed pattern with a cleared sign, discarding both the payload and the sign of
the argument. Nothing caught it, because the result still *classified* as a NAN
and every existing assertion was satisfied by that. What is lost is the point of
a payload -- it records where the trouble started, and a chain of operations
that passed through one transcendental turned a traceable NAN into an anonymous
one.

§4.5.4.2 settles the signalling case the same way: "the SNAN is converted to a
non-signaling NAN (by setting the SNAN bit in the operand to a one), and the
operation continues as described in the preceding section". So it is the same
value with one bit set, not a fresh NAN, and the payload survives quietening.
Both are now asserted **against `FADD`'s own result** rather than against a
written-down expectation, since §4.5.4's claim is precisely that the two behave
alike -- a test that pinned the transcendentals' answer independently could
drift from the arithmetic without failing.

The constructed NAN stays where it belongs: an *operand error* -- `FLOGN` of a
negative, `FASIN` outside the unit interval -- has no source NAN to carry
forward, and §6.1.3's trap-disabled result is a fresh one.

**Underflow was measured against extended's limits rather than the rounding
precision's**, which is the exact mirror of the overflow gap and was missed for
the same reason: extended's thresholds look like the only ones there are.
§6.1.5's NOTE says otherwise -- "an underflow can occur when the destination is
a floating-point data register and the selected rounding precision is single or
double **even if the intermediate result is large enough to be represented as an
extended precision number**" -- and §3.6 gives the ranges, `0 < e < 2047` biased
by 1023 for double so a minimum of -1022, and -126 for single. `2^-200` is an
ordinary extended number, subnormal at single and perfectly normal at double;
without the threshold it kept an exponent no single-precision destination could
encode and reported nothing at all.

The order matters as much as the threshold. §6.1.5: "the intermediate result is
checked for underflow, rounded, and checked for overflow before it is stored",
and "the denormalized intermediate result is [then] rounded to the selected
rounding precision". So the denormalising shift happens *before* the rounding.
Doing it after would round twice -- once at the intermediate's own position and
again after shifting -- which is the double-rounding error this core already
takes care to avoid elsewhere.

**Chasing the golden's change then found a spurious exception.** The digest
moved, which was expected, but the scan of *which* results moved showed `FCOSH`
reporting `UNFL` -- and `cosh` is never less than one. It forms `e^-|x|`, which
underflows for any large argument, and was collecting that. §6.1.5 defines
underflow by "the intermediate result of an arithmetic operation ... too small
to be represented", meaning the operation's own result; an exception raised for
a step the caller cannot see is worse than none, since a handler traps on an
answer that is perfectly representable and nothing in the value hints at why.
`sinh` had the same fault on the same path. Only `OVFL` now propagates from
those internal exponentials, which is the one that genuinely does reach the
result.

The golden was re-taken only after both builds were checked against each other
again -- `-O0` and `-O3` agree at `0xDFE1312935332E88` -- and the scan that
identified the moved results is what distinguished the intended change from the
accidental one. A digest that had simply been updated to whatever the new build
produced would have committed the `FCOSH` bug alongside the fix.

**The inexact trap has its own equation, and the plain bit test was right only
by luck.** §6.1.10 gives it explicitly:

    Inexact Trap =
      [[EXC(OVFL) v EXC(INEX2)] ^ ENABLE(INEX2)] v [EXC(INEX1) ^ ENABLE(INEX1)]

Two things set it apart from every other exception. `ENABLE(INEX2)` is consulted
for an **overflow**, so a program that enables the inexact trap is trapped by an
overflow whether or not `INEX2` is set alongside it. And the two inexact bits
share one vector -- §6.1.7: "only one inexact exception vector number is
generated by the FPCP. If either of the two inexact exceptions is enabled, the
MPU fetches the inexact exception vector."

`ap_m68882_exception_enabled` is a bit-against-bit test, and for the inexact
case it gave the right answer *only because* this core's overflow also sets
`INEX2`. That is what IEEE 754 requires, and §6.1.7 defines `INEX2` by the
mantissa having too many bits -- which an overflow need not -- so §6.1.10's
separate `OVFL` term is there for a reason. Two modules were agreeing by
coincidence across a boundary neither states, and a later change to the
overflow path would have broken the trap decision silently. The equation is now
transcribed, and a test row sets `OVFL` *without* `INEX2` to exercise the term
that would otherwise never be reached.

Nothing else on this thread turned out to be wrong: the accrued byte's five
equations match §6.1.10 exactly, including `AEXC(UNFL)`'s AND where every other
is an OR, and `AEXC(INEX)`'s own overflow term.

**The conditional predicates were missing entirely, and with them `BSUN`.**
The decoder recognised `FBcc`, `FDBcc`, `FScc` and `FTRAPcc`, §10.7.1 priced
them, and `BSUN` had a bit position and accrued into `AEXC(IOP)` -- but nothing
could answer whether a condition was true, and nothing ever set `BSUN`. A whole
corner of the programming model was named without being implemented.

§4.4's three tables give thirty-two predicates, and the useful discovery is that
they are **sixteen equations twice**. Every predicate in `$10-$1F` is its
partner in `$00-$0F` with bit 4 set, and the pair share an equation exactly:
`OGT` at `$02` and `GT` at `$12` are both `~(NAN v Z v N)`. What bit 4 selects is
not a different test but a different attitude to an unordered operand -- §4.4.2's
aware tests "do not set the BSUN bit ... under any circumstances", §4.4.1's
non-aware ones do. So the implementation is sixteen equations plus one bit, and
`BSUN` reduces to bit 4 against the NAN condition code with **no special cases
at all**.

§6.1.1 phrases that rule as "except EQ and NE", which reads like a special case
and is not one: both live at `$01` and `$0E`, in the low group, so the encoding
already excludes them. Recognising that is the difference between a rule and a
list of exceptions.

**The test design caught a transcription error immediately.** Rather than
restating each equation -- where a mistyped one agrees with itself -- the test
names what each predicate *means* over the four states a comparison can leave
(greater, equal, less, unordered) and lets the table answer. `UGT` at *equal*
came back true, and "unordered or greater than" cannot be. The overbar in the
manual spans `(N v Z)` and I had read it over `N` alone. Thirty-one rows were
right; one was not, and only an independent statement of meaning could tell
which.

The suite also pins the manual's own warning, since it is the reason the aware
set exists: "compiler programmers should be particularly careful of the lack of
trichotomy in the floating-point branches". With an unordered operand `FBGT` and
`FBLE` are **both false**, so inverting a condition is not the same as negating
it -- and both raise `BSUN`, which is how a non-aware program finds out.

**The predicate evaluator was built and left uncalled -- the same mistake as
the 68851 write-back, two iterations after learning it.** `ap_m68882_execute`
rejects anything that is not a general-type instruction, so nothing in the core
could reach the thirty-two predicates at all. Running the check that caught it
last time -- *does anything call it?* -- found it immediately, which is the
argument for keeping that check as a habit rather than as a one-off.

It is wired now through `ap_m68882_condition`, and that entry point is the
*whole* of the part's contribution to `FBcc`, `FDBcc`, `FScc` and `FTRAPcc`.
§9's protocol has the main processor write the predicate to the condition CIR at
`$0E` and read the answer back; fetching a displacement, decrementing a
register, taking a trap or writing a byte of ones is the MPU's own work. So
`ap_m68882_execute` still reports those instruction *types* unimplemented, and
that is now an honest boundary rather than a gap: the coprocessor side is
complete, and what is missing is the 68030's half of a dialog it does not yet
hold.

`BSUN` goes through the same `apply_exceptions` path as every other exception
rather than being written into the FPSR directly, which is what keeps it
accruing into `AEXC(IOP)`. The test drives a real part -- `FTST` of a NAN, then
a non-aware predicate -- and reads the status register back, because a unit test
of the evaluator is exactly what could not see the wiring was missing.

**An enabled floating-point exception now becomes a trap.** `ap_m68882_exception_enabled`
and `ap_m68882_inexact_trap` were for a long time reachable only from tests,
recorded here as "interface, not mechanism, and there is no 68030-side FPU trap
path yet to call them". There is now, and the gap was found by sweeping for
public functions the *product* never calls rather than by anything failing —
nothing failed, because a trap that is never delivered breaks no test that does
not ask for one.

Three orderings meet in this and none of them is the FPSR bit order, which is
the trap the implementation is shaped around:

- **Which exception traps** is `[FPCP]` §6.1.9's priority — `BSUN`, `SNAN`,
  `OPERR`, `OVFL`, `UNFL`, `DZ`, `INEX2`/`INEX1`, highest first — and "only the
  highest priority exception trap is taken; the other enabled exceptions do not
  cause a trap".
- **Which vector that is** is `[030]` Table 8-1 (p. 8-3): 48 `BSUN`,
  49 Inexact, 50 `DZ`, 51 `UNFL`, 52 `OPERR`, 53 `OVFL`, 54 `SNAN`. `INEX1` and
  `INEX2` share 49, §6.1.10.
- Neither is the bit order, so `48 + bit` and `48 + priority` are both wrong.
  The two mappings are written out, and split across the two modules: the FPCP
  answers *which exception* (`ap_m68882_trap_exception`) and the MPU answers
  *where it vectors* (`ap_m68030_fpu_trap_vector`). That split is also what
  keeps the dependency one-way — `m68030` includes `m68882`, never the reverse.

**The trap is not taken by the instruction that caused it.** §6.4.2 (p. 6-33):
with `EXC PEND` true and "an attempt ... made to initiate an FPCP instruction
(other than an FMOVEM, FMOVE control register, FSAVE, or FRESTORE), the response
CIR is encoded to the take pre-instruction exception primitive". The part runs
concurrently with the MPU, so delivery waits for the next non-exempt
floating-point instruction — and being *pre-instruction*, it stacks that
instruction's own address so `RTE` re-attempts it rather than skipping it.
`ap_m68030_stacks_next_instruction` gained the FPCP range for exactly that.

The exempt four are the ones a handler needs; §6.1.9 tells handlers to move data
with `FMOVEM` because it "cannot generate further exceptions", and if `FMOVEM`
reported the pending trap the first instruction of every handler would re-enter
it.

`EXC PEND` is **derived** as `EXC & ENABLE` rather than latched. The manual's own
account of clearing it is what makes that truer: this part does not clear it on
acknowledge at all — "the MC68881 detects the exception acknowledge, [and] clears
EXC PEND. However, the MC68882 does not clear the EXC PEND bit" — and what a
handler clears it *with* is a write to the FPSR, which deriving honours by
construction. The stated cost: enabling a trap in the FPCR *after* an exception
was recorded arms it here, where a latch set at the moment of occurrence would
not. §6.4.2 leans that way ("a programmer can make exceptions pending in the
FPCP under software control") but it is a reading, recorded as one.

Verified by three tests in `step_suite`, chosen so that each catches an error
the other two would let through: the divide-by-zero traps on the *following*
`FADD` and not on the divide, `FMOVEM` runs with the trap still pending, and a
*disabled* exception sets its FPSR bit and traps nothing — the last being the
case nearly every real program is in.

**And by an oracle probe, which is where it stops being our word for it.**
`fpu-trap` enables `DZ`, divides 1.0 by 0.0, and has its own handler store the
stacked format word from the `FADD` that must never run. `$000000C8` carries
three separate claims in one value: that an enabled exception traps at all, that
it traps through **vector 50** — neither `48 +` the FPSR bit (`DZ` is bit 10) nor
`48 +` its place in the priority order (sixth), so a wrong mapping lands on a
different vector and stores a different number — and that the frame is
format 0, four words, which is what a pre-instruction exception takes.

This core returns `$000000C8`. **MAME returns nothing**: it runs all nine
instructions including the `STOP` this core never reaches, and leaves the
sentinel at its fill. That is classified *oracle-wrong* from MAME's own source
rather than inferred from the behaviour — `m68kfpu.cpp` raises exactly two
exceptions, an F-line for an unimplemented encoding and a `TRAPV` for `FTRAPcc`,
and never a vector in 48–54. Its FPCP has no exception traps at all.

So this is the "hardware-truer than the oracle" class, with three citations and
the oracle's source agreeing on what is absent: `[FPCP]` §6.4.2 for the delivery
mechanism, §6.1.9 for the priority, `[030]` Table 8-1 for the vector.

**The condition codes themselves came back correct against Table 2-1**, all ten
data types including the two that catch a careless implementation: `N` is the
sign of the mantissa and is set *independently of the type*, so a negative zero
is `N` **and** `Z` and a negative NAN is `N` **and** `NAN`. A model that treated
`N` as meaningful only for a normalized result would clear it in exactly the two
places a program is most likely to be checking a sign. The manual's own framing
is that "the FPCP generates only eight of the 16 possible combinations", the
other eight being unreachable because `Z`, `I` and `NAN` are mutually exclusive
-- and that exclusivity is now asserted rather than assumed.

§2.3.1 also states the four IEEE conditions independently -- `EQ = Z`,
`GT = ~(N v NAN v Z)`, `LT = N ^ ~(NAN v Z)`, `UN = NAN` -- which is a second
statement of the aware predicates at `$01`, `$02`, `$04` and `$08`. The table
transcribed from §4.4 agrees with it, which is the kind of corroboration worth
having after a transcription error was found in that very table.

**What had never been tested is the chain.** A comparison produces a result, the
result's data type sets the condition codes, a predicate reads them, and the
answer is what a branch acts on. Each half had been checked against the manual
separately -- and two halves that are individually right can still disagree
about what they mean by `N`. The suite now runs `FCMP` over greater, less,
equal and both orders of unordered, and asserts that exactly one of the four
IEEE conditions holds in every case. Exactly one is the property that makes them
the IEEE conditions rather than four independent tests, and it is not implied by
either half alone.

**One approximation is recorded rather than closed.** At *extended* precision
all four rounding modes return the same value here, because the model computes a
64-bit approximation directly and has no bits below the destination left to
round; the part carries 67 bits internally and its directed modes differ.
**Now measured rather than theoretical** (`FINDINGS.md` C63, C64): it costs
one unit in the last place on `FSIN`, `FTAN` and `FETOX` at argument 1.0,
always low and never high -- the kernels sum positive terms of decreasing
size, so each discarded tail can only pull the running total down -- and it
is the whole of why MAME is the closer implementation on those three. The
divergence is at most one unit in the last place -- a sixty-fourth of §4.3.2's
typical bound and a four-thousandth of its worst case -- and closing it would
mean carrying guard bits through every kernel, for a gain smaller than it looks
given the part's own directed rounding of a transcendental is accurate only to
the same published bound. A test asserts the no-op so that anyone who later adds
guard bits finds it and deletes it deliberately.

**`FMOD` and `FREM` are in.** Calling them an "honest boundary" last iteration
was wrong: unlike the MPU-side conditional dialog, they have no external
dependency and are *exactly* specified, so §4.3.2's bound has nothing to say
about them and neither did anything else. They were an unimplemented feature on
a ticked item.

The two differ in one thing only -- `N = INT(FPn / Source)` rounded to nearest
for `FREM` and to zero for `FMOD` -- and the manual insists the difference
matters: `FMOD` "uses the round-to-zero mode and thus returns a remainder that
is different from the remainder required by the IEEE Specification". For `5` and
`3` the modulo is `+2` and the IEEE remainder is `-1`.

Both are **exact**, and that shapes the implementation. A remainder is always
representable -- smaller than the divisor, sharing its exponent range -- so
neither can round, overflow, or raise `INEX2`. They are computed by restoring
long division on the significands rather than as `a - b * round(a/b)`, because
the quotient can be astronomically large: `1e10 mod 3` needs a 34-bit quotient,
and forming it as a floating-point value and multiplying would be wrong by more
than the answer. Verified against a 80-digit reference over 144 cases, value and
quotient byte, in both modes.

The quotient byte is §2.3.2's, and two of its properties are easy to get
backwards. Its sign "is the exclusive OR of the sign bits of the source and
destination operands" -- the sign the *quotient* would have, not the remainder's,
which follows the dividend; the two agree in exactly the half of cases that
hides a mistake. And it is not cleared at the start of an operation the way the
exception byte is: "the quotient bits remain set until they are cleared by the
user, or until another FMOD or FREM instruction is executed", so it is written
in the instruction rather than in the shared tail.

**`step_suite`'s example of an unimplemented form has moved twice**, and the
second move was the lesson. It was `FSIN` until the transcendentals landed, then
`FMOD` -- chosen with the reasoning "pick a gap that will stay open" -- and that
closed too. It now points at an *architectural* boundary instead: an opclass
`010` form, whose operand comes from memory. That one cannot close by
implementing an operation, since §9 has the main processor evaluate the address
and transfer the operand, so when it does close the test should be deleted
rather than repointed.

**`FSGLMUL` and `FSGLDIV` are in, and they forced a distinction the rest of the
core did not need.** §6.1.4: for these two "the rounding precision programmed in
the mode control byte is **ignored** (although the selected rounding mode is
used)", and both §6.1.4 and §6.1.5 add that "although the mantissa of the
intermediate result is rounded to single precision, the exponent remains an
extended format exponent. Therefore, those instructions can never report an
overflow as long as the intermediate result is small enough to be represented in
extended precision format."

So a single parameter cannot serve: `finish` now takes a rounding precision
*and* a range, which are the same for every other operation and differ only
here. `2^200` is the case that separates them -- an ordinary multiply at single
precision overflows it to an infinity, `FSGLMUL` keeps it. Folding the two
together would have agreed with the part on every ordinary operand and differed
on exactly the ones these instructions exist for.

**How many bits the input truncation keeps is a reading, and it is recorded as
one.** §6.1.4 says "each mantissa is truncated to 23 bits"; the `FSGLMUL` page
says operands are "assumed to be representable in the single precision format"
and that accuracy is unguaranteed if one "requires more than **24** bits of
mantissa". Twenty-four is single precision's significand -- one integer bit and
23 of fraction -- so the two reconcile if §6.1.4 counts the *fraction* field,
which is what a single-precision number stores. Read literally as 23 significand
bits, the truncation would discard a bit the instruction page calls
representable and the two pages would contradict each other. Twenty-four are
kept. It affects only operands the manual already places outside the
instruction's contract.

**A stale build produced a false green, and the timestamps caught it.** After
wiring the two instructions I ran `ctest` without rebuilding -- the edit and the
standalone check had happened in one step, the cmake build in another -- and 111
suites passed against a binary that predated the change. The guard asserting
those instructions were still unimplemented should have failed and could not.
Comparing the test binary's mtime against the source's is what showed it; a
green run is only evidence if the thing that ran is the thing that changed.

Every general-type operation the 68882 defines now executes. What remains is not
an operation but a *dialog*: §9 has the main processor evaluate an effective
address and transfer the operand, so any opclass other than register-to-register
still reports our gap. The guard now asserts that **boundary** rather than a
list of instruction names -- it needed editing twice for exactly that reason --
and it closes when the 68030 holds up its half.
so the two kinds of gap are not conflated.

So the gap is now specified rather than merely declared. The nineteen
transcendentals are classified by the four families §4.3.2 names, with `FSQRT`
excluded by its own parenthesis; the bounds are constants; and a test asserts
that each of the nineteen still reports `UNIMPLEMENTED` rather than
`TAKE_LINE_F` (which would claim a valid encoding invalid) or `EXECUTED` (which
would mean an approximation had been added without meeting the criterion). A
control test on `FSQRT` stops that passing for the wrong reason. Closing the gap
now costs a failing test that points at the acceptance criterion; leaving it
honestly open costs nothing.

One trap for the exception path: the unimplemented-instruction exception and the
F-line illegal exception **share vector 11**, and "the exception handler uses the
stack frame format ($0 or $2) to distinguish between the two". The frame format
is the only discriminator, so pushing the wrong one would send a legal `FSIN` to
the illegal-instruction handler and kill a process that should have had its sine
computed in software.

**The floating-point programming model is shared, and that is recorded as
evidence rather than assumed.** §9.1 states it: "The MC68040 FPU is compatible
with the MC68881/MC68882." Duplicating `FPCR`, `FPSR` and their encodings would
create two descriptions of one thing, and the copy no booting machine exercised
would drift -- the same argument that made `ap_cpu_decode()` a wrapper rather
than a second decoder. So `m68040_fp_model_suite` checks the 68040 manual's own
statements (Table 9-1's rounding encodings, §9.2.2.2's 24/53/64-bit boundaries,
Figure 9-5's exception bit positions, §9.2.2's reset defaults) against the 68882
modules already in the core. If the parts ever turn out to differ, a test fails
and the sharing gets revisited instead of being silently wrong.

The distinction the suite keeps straight: the programming model is common and
the *instruction set* is not. `FSIN` uses the same registers, rounding mode and
exception bits on both parts -- and executes on one while trapping on the other.

**The pipeline's timing composition is in, and it is a different shape from the
68030's.** Phase 2 modelled the 68030 as §11.6's `(r/p/w)` triples composed by
Equations 11-1 and 11-2 with head and tail overlap. The 68040's tables cannot be
read that way:

- **Three stages are priced separately** -- `<ea> calculate`, `<ea> fetch`,
  `execute` -- rather than one figure per instruction.
- **The fetch stage is not in the tables at all.** It is derived from Table
  10-2's access counts, "and an instruction requires one clock to pass through
  the <ea> fetch stage even if no operand is fetched" -- the floor is what a
  naive reading of "one clock per access" drops, since `Dn` costs zero accesses
  and one clock.
- **Execute time is two numbers.** "Presented as a lead time and a base time",
  written `nL + b`, where the lead is how long the instruction may stall on
  entering the execute stage for free.

The lead is worth carrying because of the interlock: for the brief and full
extension word modes, a stall *beyond* the lead lengthens the `<ea> calculate`
stage by the excess. The manual's worked example writes that as "3 - 1 = 2L",
which is loose arithmetic -- three clocks of stall against two of lead gives one
clock of increase -- but the rule it states in words is unambiguous, and that
rule is what is modelled. A related consequence: `BR = PC` adds "1 and 1L clocks
to the <ea> calculate and execution times", so its execution cost lands on the
*lead* and buys stall tolerance rather than costing a clock outright.

Two of §10.1's four suppositions bound how far any of these numbers can be
trusted, and both are recorded rather than silently assumed: "all memory
accesses hit in the caches; no table searches occur as a result of ATC misses",
and misaligned `<ea>` fetch timing is left to the reader entirely. So a table
figure is a best case, not a measurement -- which matters for a core whose whole
claim is that its timing is provable.

**§10.5's table is transcribed** -- 75 rows over 44 instructions -- and it
produced the sharpest illustration yet of why the page images are mandatory.
`pdftotext` renders this table's `Bcc` as `Bee` and `NOP` as `NOpa`, which are
obvious damage. It also renders **`MOVEQ` as `MOVEa`**, which is not: `MOVE` is
a real instruction with its own row in §10.4, so a table built from the
extraction would silently have given `MOVE` the timing of `MOVEQ`. The figures
survive extraction; the instruction names do not.

Three of the section's six notes qualify figures rather than explaining them, so
each row carries how far it can be trusted: note `a` makes a figure a
**minimum**, note `b` a **typical**, and note `e` marks `PTESTR`/`PTESTW` as a
"typical measurement for three-level table search with no descriptor writes, no
entries cached, and four-clock memory access times" -- one search against one
memory, which a different machine would not reproduce. Reporting any of those as
exact would claim a precision the manual withholds.

Note `d` is the one no per-instruction figure can carry: "successive in-line
MOVE16 instructions each add eight clocks to the <ea> calculate and execute
times", a cost that depends on the *previous* instruction. It is flagged on the
`MOVE16` rows so a scheduler knows to look rather than trusting the row alone.

**§10.3's cache maintenance timings are formulae, not numbers**, and the
section says why. `Idle` is "the number of clocks required for all pending
writes and instruction prefetches to complete" and depends on what ran before;
`Line` is "the number of clocks required in the user's system for a line
transfer" and depends on the memory the part is soldered to. For `CPUSH` the
manual declines an equation outright: "it is impossible to provide an equation
for execution time that works for all code sequences." So both parameters stay
the caller's to supply -- folding a guessed `Line` into a constant would invent
the one number the manual explicitly withholds.

Two structural facts fell out of the transcription and both check it:

- **`CPUSHP`'s worst case is `11 + 256 x Line + Idle`,** and 256 is every line
  in the cache -- 64 sets of four ways from §4.1. The timing table and the cache
  chapter state the same geometry without either citing the other, so the test
  asserts they agree rather than hard-coding 256 twice.
- **Each best case is its own worst-case formula** at the cheapest line transfer
  that form allows: `CPUSHL`'s 6 is the formula at `Line = 0`, and `CPUSHP`'s
  267 is `11 + 256 x 1`. The difference is real -- with no dirty entries
  `CPUSHL` transfers nothing, while the page form must still examine all 256
  lines to discover they are clean. An exact match on a three-digit figure is
  strong evidence both rows were read correctly.

One oddity worth noting from Table 10-3: **`CINVA` costs no more than `CINVL`**
(9 clocks each) while `CINVP` costs 266. Invalidating everything needs no
search; invalidating one page must examine every line to find the ones that
page owns.

**§10.4's `MOVE` cross product is transcribed** -- 15 source modes against 12
destinations, 180 cells. `MOVE` is the only instruction the manual prices this
way, because it is the only one whose two effective addresses are both general.

The source and destination columns name different things and it is not an
inconsistency: sources are `BR`-relative (`(bd,BR,Xn)`) and destinations are
address-register-relative (`(bd,An,Xn)`), because a `MOVE` destination cannot be
program-counter relative. That is why the table has `(d16,PC)` and `(d8,PC,Xn)`
source rows and no PC-relative destination at all, and why §10.1's supposition 1
applies to the source alone.

At 180 cells, spot checks are not enough on their own, so the suite also asserts
structural properties that a displaced row or column would break:

- For every destination from `(bd,An,Xn)` onward, the eight source rows with no
  index register carry **identical** figures -- the destination's own address
  calculation dominates. One displaced row in a column of eight identical values
  shows up here and nowhere else.
- A `(d16,PC)` source costs more than `(d16,An)` in *every* column, by a margin
  that grows with the destination's complexity -- supposition 1's `1L`
  compounding through the interlock rather than a flat penalty.
- Every postindexed `([bd,...],Xn)` form carries a three-clock lead where the
  preindexed forms carry one: the extra indirection must complete before the
  index applies, and the lead is where that shows.

The table is generated from the figures as read, one designated initialiser per
cell, so a misplaced row cannot shift a whole column silently.

**§10.6 is under way, and page 10-14 forced a model change.** The shift and
rotate rows print *two* figures in one cell -- `ASL Dn` is `3/4` -- under the
footnote "immediate count specified for shift count/shift count specified in
register, respectively". A cell type with one execute figure cannot hold that,
and picking either value would be wrong half the time: a model that took the
first would under-price exactly the register-count shifts a compiler emits most.

The distinction applies only to the `Dn` row, because a memory shift is always
by one, and a test asserts that -- so a caller may ask for the register-count
figure unconditionally and get the right answer for every other mode.

Two groupings in this section carry information beyond the timings. `ASL` is
alone in its column at 3 clocks where `ASR`, `LSL` and `LSR` share one at 2 --
it is the only shift that must detect overflow, the others having no `V` to
set. And `ADDQ`/`SUBQ` are a separate column from `ADDI` and friends precisely
because they *do* accept an `An` destination where the immediate forms do not.

Page 10-15 forced the model wider again, twice over.

**Three different distinctions share the `a/b` notation.** §10.6 prints two
figures in a cell for three unrelated reasons, and conflating them would price
one instruction by another's rule:

| group | what the second figure means |
| --- | --- |
| `ASL`, `ASR`… | shift count in a register rather than immediate |
| `BCHG`, `BCLR`, `BSET` | bit number in `Dn` rather than `#<xxx>` |
| `BFCHG`, `BFEXTS`… | width **and/or** offset in a register |

The last is the subtlest: it is not "the offset is in a register" but "width
and/or offset", so *one* register operand out of two already costs the higher
figure. The cell records which distinction applies, so a caller cannot ask the
wrong question of a cell.

**And the calculate column can be dual too, running the other way.** `BCHG
(d16,An)` prints `2/1` for calculate against `1L + 3/4` for execute -- a
register bit number is *cheaper* to calculate and dearer to execute. A model
with one calculate figure could not express it, and one that assumed both
columns move together would get the sign wrong.

**The bit-field boundary penalty is a penalty, not a figure.** Note c: "if the
bit field spans a long-word boundary, add ten and nine clocks to the <ea>
calculate and execute times, respectively." That depends on the operand's
*address*, not its encoding, so no static table can fold it in. Note d gives
the extract instructions a different penalty -- two clocks, and to execute only
-- because an extract reads two long words where a change must read and write
both. Sharing one penalty between the groups would be wrong by a factor of nine.

One protection fact surfaces in the timing table: `BFEXTS`/`BFEXTU` accept the
PC-relative modes and `BFCHG`/`BFCLR`/`BFSET` do not, because the first pair
reads a field and the second writes one.

### A note whose letter points at the wrong text

Page 10-16 marks its `Dn` row `3/4^d` and `6/7^d`, and its note `d` reads "if
the bit field spans a long-word boundary, add ten and nine clocks…". **A data
register has no long-word boundary**, so that cannot be what the superscript
means. The glyph was read at 500 dpi to rule out a misread letter.

The correct reading is the one page 10-15 prints for the identical figures:
`BFCHG Dn` is also `3/4` and `6/7`, marked `e`, whose note is "immediate count
specified for both width and offset and width and/or offset specified in
register, respectively". Three further facts support it -- no group header on
page 10-16 references note `d` (they carry `a,b`, `a,c` and `a`), so `d` exists
solely for the `Dn` row, which is where a selector note belongs.

Sources exhausted in order: the `MC68040 Designer's Handbook` summarises §10.6
without the per-instruction notes, and neither official errata document --
`MC68040UMAD` nor `MC68040UMAD2`, both fetched -- mentions §10.6 at all. So the
letter is wrong in the manual, and the modelling follows page 10-15.

That reading earned its keep immediately. The test asserting *"a register
operand never pays the boundary penalty"* caught a real bug in the table
generator, which had applied each group's penalty to every row including `Dn`.
Had I taken note `d` at face value the bug would have looked correct.

The three bit-field groups also carry genuinely different penalties -- `+10/+9`
for the changing group, `+7/+7` for `BFINS`, `+2` execute-only for `BFFFO`, and
none at all for `BFTST` -- so a single shared penalty would be wrong for four of
the five.

Page 10-17 added a third way a column can be qualified. Note `d` on `CHK`
reads "times listed are for `Dn` **within bounds**" -- so the column prices the
case that does *not* trap. That is neither a minimum nor a typical figure: a
failing check takes an exception whose cost is §10.5's, and adding the two would
double-count the operand fetch. Recording it as its own class keeps a scheduler
from treating the column as a lower bound on `CHK` in general.

`CAS` is the first column marked *typical*, and the reason is in its note: it
"synchronizes some portions of the processor before execution". An indivisible
read-modify-write cannot have one figure. Its cost is also the largest yet
transcribed -- 36 clocks to calculate and `6L + 31` to execute for `CAS (An)`,
against one and one for `ADD (An)`. That is not a variant of an ordinary
access; it is two orders of magnitude of work, which is worth knowing before
any DN5500 lock contention is measured.

The read/write pattern held for a third page and in its cleanest form:
`BTST` takes the PC-relative modes that `BCHG`, `BCLR` and `BSET` are denied.
Same bit, same addressing, different protection -- because one tests and the
others write back.

Page 10-18 forced the penalty model open. `CHK2`'s footnote carries **two**
conditions at once: "for UB < LB, add three clocks to <ea> calculate and execute
times. For Rn = An, add one clock". Neither is derivable from the encoding --
`UB < LB` is a relation between two bounds held in memory -- and both can hold
together, so a single pair of numbers cannot express them. Penalties are now a
tagged list, and a caller may pass every condition it knows about because only
the ones a cell names take effect.

That tagging also made an existing test more precise. It had asserted "only the
bit-field groups carry a penalty", which `CHK2` immediately falsified; the
honest statement is that only they carry a *spans-long-word* penalty, and the
tag is what lets the test say so.

Two smaller observations from the same page. `CHK2` has no register or immediate
form at all, where `CHK` has both -- it compares against a bound *pair* in
memory. And `CLR` and `CMP` share every figure they both define: `CLR` lacks the
PC-relative, immediate and `An` modes because it only writes, but where both are
valid the cost is the addressing rather than the operation.

### A cell that prints a cost where no instruction exists

Page 10-19 prints **`0`** -- not a dash -- for `CMP2 (An)+` and `CMP2 -(An)`, in
both columns, read at 500 dpi to be sure. `CHK2`'s column on the previous page
dashes the same two rows, so §10.6 contradicts itself.

The `M68000 Family Programmer's Reference Manual` settles it twice. `CHK2`'s
effective address field takes "only control addressing modes" and its table
dashes `(An)+` and `-(An)` explicitly; and of `CMP2` it says "this instruction
is identical to CHK2 except that it sets condition codes rather than taking an
exception when the value in Rn is out of bounds", so the two share their
addressing modes by definition. A third argument needs no source: no valid cell
anywhere in §10.6 costs zero clocks, because an instruction that executes takes
at least one.

So the `0` means "not applicable" and is modelled as invalid, with a test
asserting `CMP2` and `CHK2` accept *exactly* the same modes -- which would catch
a later page being misread before anything else did.

One inversion worth noting: `CMP2` costs **more** than `CHK2` for the same
addressing (13 against 11 to calculate `(An)`), which is the opposite of the
intuition that a trapping instruction must be the dearer one. Setting condition
codes is work; taking a trap that does not happen is not.

A second plausible invariant of mine failed here, after `ADDA`: I assumed `CMPA`
costs the same as `CMP`, since a comparison discards its result. The table
refutes it at `(An)+`, `-(An)` and `(d16,An)` -- the column is headed `CMPA.L`,
so it always reads a long word where `CMP` may read a word. Twice now a
"the A form is the base plus a constant" rule has proved false, which is a
reason to keep transcribing rather than interpolating.

Page 10-20 introduced a fourth kind of conditional cost, and the first that
**replaces** a figure instead of adding to it. The divide columns note:
"execution time for a DIV/0 exception taken and exception processing is
approximately 16 + <ea> calculate clocks."

The trap is the direction. `DIV.W #0,Dn` costs about 24 clocks, which is *less*
than the 27 a completed `DIV.W` costs -- the division never happens. Modelling
this as a penalty added to the normal time would give 51: more than twice the
truth, and wrong in sign. The manual's own worked example is what pins the
reading, since 16 + 8 = 24 comes out even only if the 16 is added to the
*calculate* figure and not the execute one.

It is also explicitly approximate -- the note says "approximately" twice -- so
it is exposed as its own accessor rather than as a figure among the exact ones.

Two smaller observations. The divides are the first columns where the execute
stage dominates the whole instruction: 27 clocks for a word divide and 44 for a
long, against one for `ADD`. And `JMP (d16,PC)` is `5L + 1` -- five clocks of
stall tolerance against one of work -- which is the clearest case in §10.6 of
why the lead is carried separately rather than folded into a total: a change of
flow has almost nothing to execute and everything to wait for.

### An anomaly transcribed rather than corrected

`JMP` prints the *same* figures for `([bd,BR,Xn])` and `([bd,BR,Xn],od)` -- 12
and `1L + 11` for both -- where every other column in §10.6 increments between
those rows, and where `JSR`, computing the identical effective address, goes 12
to 13. Read at 450 dpi.

This is transcribed as printed and **not** corrected, which is the difference
between it and the `CMP2` zeros. There the PRM proved the addressing mode does
not exist, so the cell could not be a cost at all. Here the figure is merely
surprising: `JMP` has nothing left to do once it has an address, so the outer
displacement's extra add could genuinely be absorbed, and no source contradicts
it. The test records the anomaly with the reasoning attached, so a later reader
meets it as a known oddity rather than assuming a typo -- and so that any
evidence which turns up has somewhere to land.

The distinction is worth stating as a rule, since §10 has now produced four
suspect cells: **correct a figure only when a source proves it impossible, and
otherwise transcribe and flag.** `FLOG2`'s omission, the misdirected note letter
and `CMP2`'s zeros all met that bar; this does not.

One genuinely surprising figure that is *not* an anomaly: `JSR (An)` costs
exactly what `JMP (An)` does, despite pushing a return address. §10.1 explains
it -- write-back times "are not listed because they are system dependent and do
not affect either <ea> calculate or execute stages" -- so the push happens in a
stage this table does not price. The subroutine call really is free in the two
stages measured here.

### A cell left blank for a mode that exists

§10.6 dashes `MOVE to SR (BR,Xn)` while pricing the mode above it
(`(d8,PC,Xn)`, 12) and the one below it (`(bd,BR,Xn)`, 14), and `MOVE from SR`
on the same page prices `(BR,Xn)` at 6. Read at 450 dpi.

The dash is provably wrong: the PRM says `MOVE to SR` takes "only data
addressing modes" and lists `(bd,An,Xn)` among them for the 68020/030/040, and
`(BR,Xn)` is that same mode field -- 110 -- with the base displacement
suppressed in the full extension word. A mode cannot be illegal two rows above
where the manual prices it with a displacement attached.

But **proving a cell wrong is not the same as knowing what it should be**, and
no source gives the figure. The dash is transcribed as printed and the gap
recorded, rather than interpolating from `(bd,BR,Xn)` and putting a number in
the core that no document supports.

That refines the rule the `JMP` anomaly established into its final form:
**correct a figure only when a source proves it impossible *and* supplies the
replacement.** Of §10's five suspect cells, `FLOG2`'s omission and `CMP2`'s
zeros met both halves; the misdirected note letter met them via page 10-15's
identical figures; `JMP`'s repeated row meets neither; and this one meets the
first half only.

Page 10-23 adds a figure shape no column can hold: `MOVEM` prints
`2 + D' + A'`, where note c defines "D' and A' indicate the number of data and
address registers, respectively (if no data registers specified the number
one)". The cost depends on the *register list* -- part of the instruction, but
neither its opcode nor its addressing mode -- so the terms are carried on the
cell and applied by the caller. The parenthetical earns its own test: `D'`
floors at one, so a save-on-entry sequence moving only address registers costs
a clock more than the arithmetic suggests.

The same page prints `3 + D' + A` in one column -- **no prime on `A`** -- where
everything around it prints `A'`, consistently down the column and confirmed at
450 dpi. Note c defines only `D'` and `A'`, so bare `A` is an undefined symbol;
it is read as `A'` because that is the only address-register quantity the
manual defines. Sixth suspect cell, and it meets both halves of the rule: the
note proves `A` undefined and supplies what it must mean.

Page 10-24's three `MOVES` columns are all *typical*, and they are the first
whose figures are **not monotonic** in addressing complexity: `([bd,BR,Xn])`
costs more than `([bd,BR,Xn],od)` in every direction. Transcribed as printed and
not corrected -- unlike `CMP2`'s zeros nothing proves them impossible, and
unlike `JMP`'s repeated row there is a ready explanation. A column marked
typical for an instruction that synchronises the processor is an average over a
variable stall, and the manual marking it so is precisely a refusal to promise
ordering.

Two asymmetries the figures make visible: reading alternate address space costs
more than twice writing it (28 against 13 to calculate `(An)`), because a read
must complete before the register is written while a write can be posted; and
loading an *address* register costs more than a data one (28 against 20), the
same sign-extension that makes `ADDA` and `CMPA` dearer than `ADD` and `CMP`.

Page 10-25 brings a **fourth** meaning for the `a/b` notation: "T1/T2 apply to
word/long-word **operand size**". It is the first that depends on the
instruction's size field rather than on where an operand lives, joining shift
count, bit number and bit-field operands. Four unrelated distinctions, one
notation -- which is why each cell records which one applies.

Two figures worth keeping. A signed multiply costs two clocks more than an
unsigned one at *word* size and exactly the same at long, so the sign is only
paid for where the operand is short enough for it to matter. And `NBCD Dn`
executes in 3 clocks against `NBCD (An)`'s 2 -- the only column in §10.6 where
the register form is **dearer** than the memory one, which inverts the
assumption every other row supports.

Seventh suspect cell: `MULS (d16,PC)` prints `2L + 16/2L + 20` while
`MULU (d16,PC)` prints plain `14/20` with no lead, though every other row pairs
the two with the same lead and `MULU` two clocks cheaper. Transcribed as
printed -- a lead is stall tolerance and no rule forces two columns to share
one, so like `JMP`'s repeated row it fails the first half of the correction
rule.

Page 10-26 -- `NEG, NEGX, NOT`, `PEA`, `ROL, ROR` -- added no new figure shapes,
and gave two facts and an eighth suspect cell.

**`PEA` is `LEA` plus a push, and three modes give the push away.** Both take
exactly the control modes, and `PEA` is never cheaper in any of the twelve they
share -- but only nine are strictly dearer. `(d16,An)` and `(d16,PC)` cost the
same with the shape unchanged: they already carry a lead, and the push fits
inside a stall the displacement fetch was paying for anyway. `(d8,An,Xn)` costs
the same with the shape *changed* -- `LEA` prints `4/4` and `PEA` prints
`1L + 3`, so a base clock became a lead clock and the total stood still. The
obvious generalisation from the first pair, "a mode with a lead pushes for
free", is refuted by the third, which has no `LEA` lead at all. Both mechanisms
are named in the test, because a reader who learns only the first will
mispredict the third.

**The rotate changes which shift it agrees with, halfway down the column.** On
the simple modes `ROL, ROR` costs 3 clocks and so does `ASL`, while
`ASR, LSL, LSR` cost 2 -- so the dividing line reads as "a bit has to be
watched": `ASL` checks for overflow, a rotate feeds the bit back round, a plain
shift drops it. On the deep modes the alignment swaps: `ROL` and `LSL` become
identical in *both* lead and base -- `1L + 7`, `1L + 8`, `1L + 10`, `1L + 11`,
`3L + 9`, `3L + 10` -- and `ASL` alone stays one clock dearer than both.
Whatever costs `ASL` its extra clock therefore survives into the deep addressing
modes and whatever costs a rotate its extra clock does not, which is the
opposite of what the simple rows suggest. Pinned rather than explained; no
source read so far accounts for the crossover.

Eighth suspect cell, and the first to fail on principle rather than on
arithmetic: `ROL, ROR` prints an `<ea> calculate` one clock *lower* than both
shift columns in all six deep modes -- 6 against 7 at `(BR,Xn)`, and so on down.
An address calculation cannot depend on what the ALU will later do with the
operand, so one of the two columns is mistyped. The page does not say which, and
nothing else in the manual repeats the figures. Both values have company:
`NEG` and `LEA` print the rotates' 6, `ADDQ, SUBQ` and `ASR, LSL, LSR` print the
shifts' 7. Kept as printed under the standing rule -- proving a cell wrong is not
the same as knowing its value. Page 10-14 was re-rendered to confirm `ASL`'s
side of the comparison before recording this, rather than trusting the earlier
transcription.

Page 10-27 -- `ROXL, ROXR`, `Scc`, `SUBA` -- produced the most serious defect
found in §10.6 so far, and the sibling-manual step is what found it.

**`ADDA` and `SUBA` are printed as two columns that disagree in seven of the
seventeen rows.** They cannot both be right. Everything else in §10.6 pairs an
add with its subtract in *one* column -- `ADD, AND, EOR, OR, SUB, TST`;
`ADDI, ANDI, EORI, ORI, SUBI`; `ADDQ, SUBQ` -- and `ADDA`/`SUBA` is the only
such pair split apart. Both pages were re-rendered and re-read before this was
recorded, so neither figure is a transcription slip of ours.

The 68030 manual settles the shape of it. §11.6.8 prints `ADDA` and `SUBA`
**identical in every entry** -- `ADDA.W Rn,An` and `SUBA.W Rn,An` are both
`4 0 4(0/0/0) 4(0/1/0)`, `ADDA.L`/`SUBA.L Rn,An` both `2 0 2(0/0/0) 2(0/1/0)`,
and likewise for the two `EA,An` rows -- and its §11.6 preamble names them as a
single pair: "the instructions with immediate operands and the ADDA and SUBA
instructions". Motorola holds the two to be timing-identical in this family.

Which 68040 column is corrupt is not proven, but the evidence points at `SUBA`.
`ADDA` is exactly "`ADD`'s calculate, `ADD`'s execute plus one" -- the cost of
writing an address register rather than a data one -- in all six deep modes.
`SUBA` follows that same rule for the first two deep modes and departs from it
for the last four. And the `Dn`/`An` execute figures are transposed between the
columns (`ADDA` 2/1, `SUBA` 1/2), which is what a typesetting slip looks like
rather than a real asymmetry: there is no mechanism by which a data-register
source would be dearer than an address-register one for one of the pair and
cheaper for the other.

Both are transcribed as printed, under the standing rule -- the sources prove a
column wrong without supplying its replacement, and the 68030's figures are in
a different unit and cannot be carried across. Two tests pin the disagreement
and the evidence behind it, so a later reader finds it loudly rather than
quietly reconciling it. This is the ninth suspect entry and the first that is a
whole **column pair** rather than a cell or a row.

Two smaller facts. **The extend rotate inverts the plain one.** `ROXL, ROXR` in
a register prints `5/6` against `ROL, ROR`'s `3/4` -- two clocks dearer -- and
in every simple memory mode prints 2 against the plain rotate's 3, one clock
*cheaper*. The reading, recorded as a reading and not as a source, is that a
memory rotate is by one, where routing a bit through X is no harder than
dropping it, while a register rotate is by a count, where each step waits on the
previous step's X. And `Scc` dashes `An` as well as the program-space modes:
it writes a byte, and an address register has no byte.

**§10.6 is complete**: pages 10-13 to 10-28, 46 column groups over 71 mnemonics
and 17 addressing modes, `m68040_iu_timing_suite` at 99 tests. Page 10-28 is
titled "Concluded" and carries one column with two empty groups beside it, so
the count is final rather than a place a later page could extend. A test pins
the group and mnemonic counts and checks that no mnemonic is priced by two
columns -- `ap_m68040_iu_find` returns the first match, so a duplicate would
leave one column permanently unreachable while silently serving the other's
figures.

That last column, `TAS`, is the most extreme in the section and the one that
best corroborates the model. `TAS Dn` costs 1/2, as cheap as a `MOVE`;
`TAS (An)` costs 26 and `2L + 24`, some seventeen times more, and it is the
widest same-column spread in §10.6 by a long way. The footnote accounts for it:
"this instruction interlocks the `<ea>` calculate and execute stages and
synchronizes some portions of the processor before execution". The price is not
the operation but the indivisible bus cycle around it, and a `TAS` to a data
register has no bus cycle to make indivisible.

**The lead means what §10.2 says it means, and `TAS` is the proof.** Across all
of §10.6 exactly three memory-indirect cells print an execute with *no* lead,
and all three belong to `CAS` and `TAS` -- the two indivisible read-modify-
writes, and nothing else. A lead is stall tolerance: how much of the execute
stage a following instruction may overlap. An operation that holds the bus
indivisibly offers none, so a zero there is the honest figure rather than a
missing one. Two atomic columns, and they are the only leadless ones in 46:
that is the strongest structural corroboration the section offers.

**Page 10-24's `MOVES` oddity was misread, and page 10-28 corrects it.** Four
columns print an `<ea> calculate` that *falls* as the addressing mode grows
more indirect -- the three `MOVES` directions and `TAS` -- and all four are
marked *typical*. No column the manual presents as exact ever does it. So the
non-monotonicity recorded earlier as an oddity is not a defect at all: a typical
figure is an average over cases, and an average has no reason to be monotonic in
anything. `TAS`'s 35 at `([bd,BR,Xn],od)` against 34 at `([bd,BR],Xn)` -- where
all 41 exact columns print those two rows *equal* -- is licensed by the same
marking and is likewise not recorded as a defect. The implication runs one way
only: `CAS` and `CMP2` are typical and monotonic, so the marking permits a fall
without predicting one. The suspect count therefore stays at nine.

§10.7 is under way, and its first two sub-sections are in
(`m68040_fpu_timing_suite`, 18 tests). They needed a new module rather than an
extension of §10.6's, because they answer a different question and index it
differently.

**§10.7.2 does not price floating-point arithmetic.** Its opening paragraph is
explicit: the integer pipeline "supports the floating-point unit by calculating
effective addresses and transferring operands", and the listed times "show the
overhead required by the integer unit to support the floating-point unit,
assuming the floating-point unit is not busy". The footnote repeats it --
"timings are for an idle FPU". So `FDIV` and `FNEG` share a single column here,
and a model that read these as the cost of an `FADD` would report a divide and a
negate as costing the same, because to the integer unit they do. What the FPU
then does is §10.7.3 and adds separately.

**The mode list is not §10.6's, in two ways.** §10.6's `An` row is replaced by
an `FPn` row -- a floating-point operation takes no address register as an
operand and does take a floating-point one -- so the two tables index different
things at that position and the enum could not be shared. And §10.6 names the
base register `BR` and prices the PC as one of its *rows*, where §10.7.2 names
it `An` and pushes the PC into a footnote: "for BR = PC, add one clock to both
`<ea>` calculate and execute times". Same six modes, opposite convention. The
added clock is modelled onto the execute *base* rather than the lead: a lead is
stall tolerance, and forming a longer address does not make the following
instruction more able to overlap.

Three facts fall out of the format axis, and each is a hardware property rather
than a timing choice. `FPn` is priced only at extended precision, because the
register file holds nothing else -- an `FPn` source *is* extended however the
size field reads. `Dn` is priced for byte/word, long word and single and dashed
for double and extended, which is the 32-bit register width and nothing more.
And the long-word and single-precision columns are **identical in all seventeen
rows**: both move 32 bits, and converting an integer or reinterpreting a single
is the FPU's work, not the integer unit's. That identity is the clearest
demonstration that §10.7.2 measures transfer.

The byte/word column differs from the long-word column in exactly one row --
`#<xxx>`, `5/3L + 2` against `3/1L + 2`. It is the one place where a *narrower*
operand is the dearer one: an immediate is fetched from the instruction stream,
so a byte or word must be extracted and widened where a long word is already
aligned to fetch.

**§10.7.1 catches a reader who trusts the mnemonic.** `FDBcc` continues its loop
when the condition is *false*, so `cc False` is the branch-taken case and prints
`11/1L + 9` against `cc True`'s `9/1L + 7`. Reading it like `FBcc` gets both
figures backwards and would make a loop cheap on every iteration but the last.
`FBcc` itself carries no lead in either direction -- a branch resolves the
instruction stream, leaving nothing behind it to overlap -- while both `FDBcc`
cases do, the decrement being work that can overlap. `FTRAPcc` is priced only
for the untaken case; taking the trap costs the exception, which §10.7 does not
price and this module does not invent.

Four preamble statements are recorded in the module header because they
constrain any later fast path: operand order is not significant for timing,
rounding *modes* never cost anything (only precision does), an `S` or `D` suffix
is a precision selection rather than a separate opcode, and every `FMOVEM` waits
for the pipe to idle before starting -- which is why `FMOVEM` is absent from the
ten-instruction column and cannot be priced by table lookup at all.

**§10.7.2 is complete** -- pages 10-30 to 10-34, seven tables,
`m68040_fpu_timing_suite` at 32 tests. Pages 10-31 to 10-34 forced the mode enum
to become the *union* of every page's rows, because the pages disagree about
which of `FPn`, `Dn` and `An` they print: the load table has `FPn` and no `An`,
the store table has `Dn` and `An` and no `FPn`, and `FSAVE`/`FRESTORE` print all
three. The row *order* also shuffles -- 10-33 prints `(xxx)` and `#<xxx>` before
`(d16,An)`, 10-34 after -- so each page's labels were read individually rather
than carried over.

**Storing an integer costs five times what loading one costs.** Loading a long
word into an FPU register is `2/2` at `(An)`; storing one back out is
`8/9L + 2`. The direction is asymmetric because the work is: a load hands the
FPU a bit pattern to convert at leisure inside its own pipeline, while a store
must have the converted integer in hand before the bus cycle can start, so the
extended-to-integer conversion lands in the integer unit's figure. The `9L` lead
is that conversion. Storing a single or double is `2/1L + 2` again -- a repack,
not an arithmetic change.

**The format columns regroup between the two directions, and the regrouping is
the finding.** Going in, the columns are `B/W`, `L`, `S`, `D`, `E`: what matters
is how many bytes are fetched, so a long word and a single cost alike. Going
out, they are `B/W/L`, `S/D`, `E`: what matters is whether a conversion happened
at all, so every integer width costs alike and the single joins the double. Two
different questions, two different groupings.

Smaller facts, each pinned by a test. The control-register move is the only
§10.7.2 table that prices an `An` row, because `FPCR`/`FPSR`/`FPIAR` are
ordinary 32-bit registers and moving one is an ordinary 32-bit move. `FMOVEM`'s
cell prices *one* register and note b adds three clocks per additional one and
one for a dynamic list -- so the count starts at one, and an eight-register move
costs seven increments, not eight; a zero-register list is refused rather than
discounted. `FSAVE` takes `-(An)` and dashes `(An)+` while `FRESTORE` does the
exact reverse, which is a stack pushed and popped. And which of the pair is
dearer *crosses over*: with real state to move, saving costs more (50 against 40
at a long frame) because the state must be gathered before any of it reaches the
bus; with a null or idle frame the order reverses (12 to save, 13 to restore),
because what is left is fixed cost and a restore must read the frame's format
word to learn what it is looking at.

Two more suspect entries, both transcribed as printed.

**Tenth: the deepest addressing mode is unreliable in both save tables.**
`([bd,An],Xn,od)` breaks the constant frame offset in `FSAVE` and in `FRESTORE`
alike, on different stages -- `FSAVE`'s calculate steps +24/+43 where its other
ten modes step +21/+38, and `FRESTORE`'s execute steps +12/+26 where its other
ten step +13/+27, because its short and long columns fail to increment from the
row above where the idle column does. Each table is the other's witness:
`FRESTORE`'s calculate holds its constant in all eleven modes, so the
constant-offset structure is real, and `FSAVE`'s holds in ten, so the exception
is not the structure. An `<ea> calculate` is address formation and cannot depend
on how many bytes of state follow it. `FSAVE`'s excess differs between its two
columns (+3 short, +5 long) with both stages moving together, so it is a row
derived differently rather than a digit slipping.

**Eleventh: `FMOVEM` dashes one program-space mode and prices the other.**
`(d16,PC)` is dashed while `(d8,PC,Xn)` is priced at `20/1L + 18`. The column
covers both directions, and the load direction legitimately takes the control
modes with both PC forms among them; there is no reading under which a PC
displacement is illegal and a PC index is legal. The neighbouring columns are
each internally consistent -- `FScc` writes and dashes both, the control-register
move reads and prices both -- which is what isolates `FMOVEM`. Same class as
§10.6's `MOVE to SR (BR,Xn)`.

**§10.7.3 is complete, and with it Section 10 and the 68040 timing item.**
Pages 10-35 to 10-37, 91 rows across eight instruction groups, in a module of
its own (`m68040_fp_pipeline`, `m68040_fp_pipeline_suite` at 18 tests).

**Half cycles are real, so the unit of this one table is the half cycle.**
`FDIV` executes in 37.5 cycles, `FMOVE` to an integer converts in 1.5 and
executes in 4.5, and one busy time is printed 12.5. Every figure is a whole
multiple of a half cycle and many are not whole cycles, so they are held as
exact integers in half cycles rather than as a floating count that would make
`37.5 + 37.5` a question about rounding. Every accessor says `_halves`: this is
the only table in Section 10 whose unit is not the clock, and a reader who
forgets is off by a factor of two.

**A stage has two times, and both are needed.** §10.7.3's opening sentence
defines a notation used nowhere else in Section 10: "times in parentheses are
the total time that the stage uses to execute an instruction even though the
stage can pass data to the next stage earlier ... `2(3)` means that the
instruction takes two cycles to execute, but this stage is actually busy for
three". The bare figure is *latency* -- when the next stage may start -- and the
parenthesised one is *occupancy* -- when another instruction may enter this one.
Keeping only the first would let two instructions share a stage; keeping only
the second would serialise a pipeline that overlaps. Both are stored, and an
unparenthesised figure has them equal, which is the honest reading rather than a
missing value.

**The cost depends on the operands' values, which is why this table could not be
folded into §10.7.2's.** A row is chosen by mnemonic, opclass, source size,
rounding precision *and* the class of the operands -- normalised, zero, infinite
or NAN. When an operand is special the result is known without arithmetic, so
execution and normalisation are priced at zero and only the conversion is paid:
a divide by zero costs 4 cycles against a real divide's 37.5 plus its stages.
Over 40 of the 91 rows short-circuit this way, and every row with a zero
execution also has a zero normalisation -- a stage that did nothing leaves
nothing to normalise.

Figures worth keeping. `FSQRT` executes in **103 cycles**, nearly three times a
divide and twenty times a multiply, and it is the largest single figure in
Section 10; its conversion and normalisation are the same `2(3)` an addition
pays, so the whole difference is the iteration. `FCMP` normalises in 1 cycle
where `FADD` takes `2(3)`, with identical conversion and execution -- a compare
performs the subtraction and discards the difference, so there is a condition
code to settle and no result to renormalise. An extended source costs exactly
one more conversion cycle than a single or double throughout, and never changes
the execution stage, because by then the operand is extended whatever it started
as. And `FMOVE` between an integer and a floating-point register prices the
*sign*: 11 cycles of occupancy for a positive source against 11.5 for a
negative, `3(9)` against `3(10)` in the other direction. It is the only place in
Section 10 where an operand's sign changes a figure -- a negation is a real step
in the conversion, not a flag.

**The two `FMOVEM` tables count registers differently, and this is a trap.**
§10.7.2's cell prices *one* register with note b adding three clocks for "each
additional" one, so its count starts at one. §10.7.3 prints "2 + (2 per reg)" --
an explicit base plus a term -- so its count starts at *zero*. Reading either
convention onto the other misprices every list, and by a different amount at
each end. The two accessors are deliberately not shared.

Two final suspect entries, twelfth and thirteenth, both in the qualifier columns
rather than the figures, and both transcribed as printed.

**Twelfth: `FADD, FSUB` prints an opclass its own block structure contradicts,
and `FCMP` is the witness.** The group is fifteen rows: three blocks of five
operand cases, one block per (opclass, size) pair. The sixth row is the
`Norm,Norm` case of the second block and prints opclass **0** with size `S,D`,
which leaves the first block with six rows and the second with four -- missing
the only case that actually computes. `FCMP` is the same fifteen-row shape with
the same five operand cases, printed two pages later, and its sixth row reads
opclass **2**; `FMUL` and `FDIV` have the same three-block structure and are
consistent throughout. So the section supplies its own replacement. The
consequence is a lookup that fails for `FADD` at opclass 2 with a double source
and two normalised operands -- the combination a program is most likely to
execute.

**Thirteenth: `FDIV` drops a size from one row.** Its opclass 2 extended block
is five rows and the `-,Inf` row prints a dash where the four around it print
`X`. A dash means "no size field applies", true of opclass 0 and false of
opclass 2 by definition. The figure itself is the block's 5 cycles, so only the
qualifier is at fault.

Both were kept as printed for consistency with the eleven entries before them,
each with a test that states the contradiction so that fixing it means deleting
a finding rather than quietly editing a number. That is bulk transcription against the
composition already in place, and the last thing standing between Phase 2b and
complete. Appendix A's bit rows have to come from page images --
`pdftotext` renders them with zeros as letters and columns collapsed, the same
failure that cost a bit position in the 68020's module entry word.

## Subsystems

| Subsystem | Status | Verification |
| --- | --- | --- |
| Build system, presets, CI | working | 4-platform matrix green on first run, plus the `-O0` vs `-O3` output-identity job |
| Model table (`model/`) | working, 9 models | `model_suite`, 18 tests |
| Time base (`time/`) | working | `time_suite`, 15 tests |
| State hash (`state/`) | primitive working | `hash_suite`, 11 tests, incl. published FNV-1a 64 vectors |
| Core board state hash (the identity harness's board half) | working: the board registers, the translation map, both interrupt controllers, the interval timer with its three clocks, the calendar with both cursors, both DMA controllers, both serial ports, the node ID, the disk and tape controllers, the graphics memories, the keyboard matrix and the boot PROM. The diagnostic counters are deliberately outside it and reported beside it | `board_state_suite`, 22 tests sweeping every device field by field |
| Full-machine state hash (`ap_machine_hash`, `ap_machine_state`) | working: the processor, main memory, the board when one is attached, and elapsed time — with the clock, the PC and the bus-error count reported beside the number | `machine_suite`, 31 tests, incl. the same workload run twice on two boards agreeing at every step |
| Ring medium interface | not started | — |
| Ring controller | not started | — |
| 68030 instruction pipe + cache holding register | working | `pipe_suite`, 14 tests, `MC68030 User's Manual 3ed` §11.2.2 |
| 68030 bus cycle state machine | working, including burst line fills | `bus_suite`, 25 tests, each citing `MC68030 User's Manual 3ed` ch. 7 (read, write and burst cycles) |
| 68030 bus arbitration control unit | working: the five-state machine of `[030]` §7.7.4, the processor at lowest priority, both documented deferrals (a committed bus cycle, and a locked read-modify-write) and the single-wire BGACK-alone path. Figure 7-61 did not survive the scan and the states are recovered from the prose walking it; one edge is marked `INFERRED` in code against the two passages supporting it. The input synchroniser is `PROVISIONAL` | `arb_suite`, 16 tests, `MC68030 User's Manual 3ed` §7.7 |
| 68030 on-chip instruction and data caches | working, including the bus-timing join: a hit costs 0 clocks, a burst line fill 5 | `cache_suite`, 30 tests and `bus_suite`, 25 tests, `MC68030 User's Manual 3ed` §6, §7.3.7 |
| 68030 integer ALU (results and condition codes) | working: ADD, SUB, CMP, AND, OR, EOR, NEG, NOT, and the shifts and rotates | `alu_suite`, 20 tests, `M68000 Family Programmer's Reference Manual 1992` Table 3-18; the byte space verified exhaustively |
| 68030 exception taking (stack the frame, fetch the vector through the VBR, load the PC) | working for the four- and six-word frames and the throwaway frame, wired to divide-by-zero, `TRAP #N`, `TRAPV`, `CHK`, `ILLEGAL`, privilege violations, MMU configuration errors, **interrupts** and **trace**; **the fault frames now build and return**, wired to bus error (vector 2) on any faulted access and address error (vector 3) on a prefetch from an odd program counter; **the coprocessor mid-instruction frame (`$9`) now builds too**, wired to the main-detected protocol violation the source operand transfer raises, with its four INTERNAL REGISTER words written as zero and marked `PROVISIONAL`; reset and the interrupt M-bit second frame decline rather than approximate | `step_suite` (10 of its tests), `exception_suite`, 16 tests, `[030]` §8.1 and Table 8-6 |
| 68030 family `0000` size-11 escape (`CMP2`/`CHK2`/`CAS`/`CAS2`) | decoded; the opcode map now has no holes. Semantics open: `CAS`/`CAS2` need an indivisible read-modify-write | `bounds_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` |
| Per-instruction timing report (`--time-instructions`) | bus and cache time only, pinned as a golden; the 0/2 alternation is the cache holding register serving two instruction words per fetch | `tests/goldens/timing.txt`; oracle side by `tools/mame-oracle/steptime.lua` |
| Probe suite (`probe/`, `--run-probes`) | 8 probes on the constructed machine, needing no firmware; results pinned as a golden under every build preset, identical between `-O0` and `-O3` | `tests/goldens/probes.txt`, `probe_suite`, 7 tests |
| Constructed machine (`machine/`) | a 68030 on flat RAM, with an out-of-range access faulting rather than wrapping; no I/O, no device, no arbitration point | `machine_suite`, 31 tests |
| 68030 published timings (§11.6) | 59 rows from §11.6.6, §11.6.8, §11.6.9, §11.6.11, §11.6.12, §11.6.15 and §11.6.16, scheduled into the step as exposed microcode + measured operand bus + prefetch exposure, since the tables show a prefetch overlaps execution while an operand the operation consumes cannot (plain `max(microcode, bus)` was the retired first model — see above and `M68030_TIMING.md`). Branches are reached through their run-time outcome rather than by opcode. Seven instructions agree with the oracle (`FINDINGS.md` C8). Rows footnoted "Add Fetch Effective Address Time" are **declined**, not part-priced: their published figure is a component and the composition is open (C9). The four divides carry the manual's data-dependent marker and are `PROVISIONAL` | `timing_table_suite`, 16 tests; both published columns checked on a running machine by `machine_suite` |
| 68030 ATC replacement | the history bit now means *recently used*, per `MC68851 PMMU User's Manual` §5.2.1.3 — a translating hit marks it, a `PTEST` probe does not. `PROVISIONAL` narrowed to victim choice among clear-history entries | `atc_suite`, 21 tests |
| 68030 prefetch marginal cost | `NCC − CC` over the published prefetch count, computed in code across every row; the two rows where it is not integral are named in the test rather than rounded away | `timing_table_suite`, 16 tests |
| 68030 effective address timings (§11.6.1, §11.6.3) | fetch and calculate rows for the non-full-format modes, with the table's `-` and "2+op head" notations carried rather than flattened. Not yet composed into the step | `ea_timing_suite`, 26 tests |
| 68030 instruction overlap (§11.3's Equations 11-1 and 11-2) | both compositions, deliberately without §11.6's per-instruction figures — those must be measured, not transcribed. The cache case through head and tail, the no-cache case by plain addition, and (11-2) shown to be (11-1) over *components* rather than a second rule | `overlap_suite`, 15 tests and `ea_timing_suite`, 12 — including both of the manual's own worked examples, at 6 clocks and **40** |
| 68030 state hash (the identity harness's CPU half) | working: every architectural register, the MMU and cache control registers, the pipe, both caches, the ATC, and the accumulated clock — host pointers excluded by construction, since `ap_hash.h` has no pointer helper | `state_suite`, 12 tests sweeping every field; `step_suite`'s same-program-twice check |
| 68030 addressing mode categories (Data / Memory / Control / Alterable) | working; derived from §2.3's definitions rather than transcribed from Table 2-4, whose Alterable column is exchanged between two row pairs in the scan | `category_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §2.3 |
| 68030 operand access (read/write through an effective address) | working; a sub-long-word operand is selected from the long word by position, and one straddling two long words is split into a bus cycle per long word in address order | `operand_suite`, 13 tests, `M68000 Family Programmer's Reference Manual 1992` |
| 68030 instruction step (fetch → decode → execute → advance) | working for `NOP`, `MOVEQ`, 8-bit `BRA`/`Bcc`, `MOVE`/`MOVEA`, the six ALU operations, the `xxxI` immediate forms, `CLR`/`NEG`/`NOT`/`TST`, `ADDQ`/`SUBQ`/`Scc`/`DBcc`, `ADDA`/`SUBA`/`CMPA`, `BTST`/`BCHG`/`BCLR`/`BSET`, the shifts and rotates, `MULU`/`MULS`, `DIVU`/`DIVS`, `ADDX`/`SUBX`/`ABCD`/`SBCD` in both the register and the `-(An),-(An)` forms, `CMPM` and all three `EXG` exchanges; everything else reports unimplemented, including divide-by-zero, which needs the exception machinery | `step_suite`, 270 tests |
| 68030 instruction prefetch (pipe driven from memory) | working | `fetch_suite`, 5 tests, `MC68030 User's Manual 3ed` §11.2.2 and §6.1 |
| 68030 logical memory access path (cache → MMU → bus) | working, reads and writes | `access_suite`, 13 tests, `MC68030 User's Manual 3ed` §6.1 |
| 68030 effective address calculation (with register side effects) | working; memory-indirect modes report the pending indirection | `addr_suite`, 13 tests, `M68000 Family Programmer's Reference Manual 1992` §2.2 |
| 68030 instruction decode dispatcher (+ MOVEQ, total length) | working — 89.9% of the 16-bit space classified, and every claimed instruction sized | `decode_suite`, 17 tests including two full 65536-word sweeps |
| 68030 family 1111 (coprocessor interface, MMU instruction dispatch) | decode working — the opcode map is now complete | `coproc_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and `MC68030 User's Manual 3ed` §9.7.6 |
| 68030 family 1110 (shift/rotate/bit field) | decode working | `shift_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 arithmetic/logic families 1000, 1001, 1011, 1100, 1101 | decode working | `arith_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0000 (immediate, bit manipulation, MOVEP) | decode working; CMP2/CHK2/CAS/CAS2 not yet covered | `immediate_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 MOVE / MOVEA (families 0001, 0010, 0011) | decode working | `move_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 single-operand group (NEGX/CLR/NEG/NOT/TST/TAS, MOVE to-from SR-CCR, ILLEGAL) | working — family 0100 now complete | `single_suite`, 7 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 LEA/CHK/`$48`/`$4C` subtree (LEA, CHK, PEA, SWAP, BKPT, EXT, EXTB, NBCD, MOVEM) | working | `misc_suite`, 11 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 `$4E` control group (TRAP/LINK/UNLK/MOVE USP/RESET/NOP/STOP/RTE/RTD/RTS/TRAPV/RTR/JSR/JMP) | working; the rest of family 0100 not yet decoded | `control_suite`, 11 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0101 (ADDQ/SUBQ/Scc/DBcc/TRAPcc) decode | working | `quick_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and each instruction page |
| 68030 branch family (Bcc/BSR/BRA) decode | working | `branch_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and the Bcc/BRA/BSR pages |
| MC68030 CPU | working: the whole opcode map decodes and all but `BKPT`, `CAS`, `CAS2`, `CMP2`, `CHK2` and the non-MMU coprocessor instructions execute. Pipe, caches, bus state machine, MMU, exceptions and bus arbitration each have their own rows below | `step_suite`, 270 tests, and the per-subsystem suites |
| 68030 operation code map (top-level instruction family) | working | `opcode_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` Table 8-2 |
| 68030 conditional tests (the 16 Bcc/Scc/DBcc/TRAPcc conditions) | working | `cond_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` Table 3-19 |
| 68030 effective address decode (modes, extension words, lengths) | decode and extension-word counts working; address *calculation* needs the instruction unit | `ea_suite`, 17 tests, `M68000 Family Programmer's Reference Manual 1992` §2, Tables 2-1, 2-2, 2-4 |
| 68030 programming model (registers, SR, three stack pointers) | working | `regs_suite`, 10 tests, `MC68030 User's Manual 3ed` §1.3 and `M68000 Family Programmer's Reference Manual 1992` §1.3.2 |
| 68030 exception vectors, priority and stack frames | working; taking an exception needs the instruction unit | `exception_suite`, 16 tests, `MC68030 User's Manual 3ed` §8, Tables 8-1, 8-5, 8-6 |
| 68030 special status word and bus fault frame layout | working: Figure 8-9's bits, the SIZ1/SIZ0 size code that counts bytes *remaining*, FC2-FC0, and Table 8-6's field offsets for both fault frames. The encoder enforces "a rerun bit is always set when the corresponding fault bit is set", while leaving a rerun *without* a fault expressible because that is how an address error is told from a bus error. The frame is chosen **from the SSW**, not passed in: §8.2.2's "data read faults only generate the long bus fault frame" is structural, since the short frame has no data input buffer for the handler to write the faulted read's value into. Fields Table 8-6 labels INTERNAL REGISTER are deliberately unnamed — this model has no source for them. **Wired into the taker**: `ap_m68030_take_bus_fault()` builds whichever frame the SSW selects, and `RTE` returns from both. Two `PROVISIONAL` approximations, marked in the code: the long frame's INTERNAL REGISTER fields are stacked as **zero** because this model has no microsequencer state, and `RTE` **re-executes** the faulted instruction from the start rather than resuming mid-instruction. The second is exact when the faulted access precedes any side effect — every case the boot PROM hits — and wrong for an instruction that had already committed one | `ssw_suite`, 11 tests, `step_suite`, `[030]` §8.2.1, Figure 8-9, Table 8-6, Table 7-3 |
| 68030 ATC (22-entry, fully associative) | working; a translating hit marks the entry recently used, a `PTEST` probe does not. Replacement `PROVISIONAL` only in its victim choice | `atc_suite`, 21 tests, `MC68030 User's Manual 3ed` §9.4 |
| 68030 descriptors + search protection state | working | `desc_suite`, 23 tests, `MC68030 User's Manual 3ed` §9.5.1.1 |
| 68030 translation control (TC) + address split | working | `tc_suite`, 15 tests, `MC68030 User's Manual 3ed` §9.7.2 |
| 68030 transparent translation (TT0/TT1) | working, bit layout now transcribed | `tt_suite`, 21 tests, `MC68030 User's Manual 3ed` §9.3, §9.7.3; layout from `M68000 Family Programmer's Reference Manual 1992` Figure 1-9 |
| 68030 MMU status register (`MMUSR`) | working, both PTEST forms, bit layout transcribed | `mmusr_suite`, 16 tests, `MC68030 User's Manual 3ed` Table 9-3; layout from `M68000 Family Programmer's Reference Manual 1992` PTEST p. 6-64 |
| 68030 translation table search (the walk) | working: search, U/M writeback, and ATC fill | `walk_suite`, 40 tests, `MC68030 User's Manual 3ed` §9.2, §9.4, §9.5, §11; writeback cost cross-checked against `MC68851 PMMU User's Manual 3ed` §5.1.5.3.11 |
| MC68851 PMMU | working as its own subsystem: the translation control and root pointers, the six descriptor formats and Figure 5-10's type determination, the status and protection registers, the 64-entry ATC, and the table search with §5.1.5.3.11's U/M write-back. The **68030's** own MMU is separate and has its own rows above | `m68851_tc_suite` 13, `m68851_rp_suite` 13, `m68851_descriptor_suite` 21, `m68851_regs_suite` 22, `m68851_atc_suite` 22, `m68851_search_suite` 26, `m68851_suite` 43; `MC68851 PMMU User's Manual 3ed` |
| 68040 MMU | not started | — |
| MC68882 FPU | working, and attached to the 68030 as a *pointer* so a machine without one keeps its line 1111 trap. Every general-type operation executes: the four arithmetic operations, the exactly-specified monadics, the remainders, the single-precision pair, and **all nineteen transcendentals** to within §4.3.2's published bound. All three operand paths run — register-to-register, **`<ea>` to `FPn`** and **`FPn` to `<ea>`**, in all six binary formats from every legal addressing mode. `FMOVEM` of the data registers runs in both directions with its reversed mask orderings, and so do the system control registers, with the FPIAR tracking under §2.4's two conditions. `FMOVECR` returns all 22 published constants, computed and correctly rounded. **Every general-type instruction executes.** **Every instruction type executes**, the conditionals included. **Every 68882 instruction and every data format executes**, `FSAVE` and `FRESTORE` included. A *busy* state frame is deliberately absent: this core's part never suspends, so nothing can generate one — for which the coprocessor's own half (`ap_m68882_condition`) is done and the 68030's dialog is not | `m68882_regs_suite` 19, `m68882_format_suite` 18, `m68882_cir_suite` 8, `m68882_round_suite` 11, `m68882_arith_suite` 41, `m68882_decode_suite` 12, `m68882_accuracy_suite` 10, `m68882_transcendental_suite` 36, `m68882_store_suite` 13, plus 51 tests in `step_suite`; `MC68881/MC68882 User's Manual 1ed` |
| MC68040 FPU | timing tables only — §10.6, §10.7.1/§10.7.2 and §10.7.3's pipeline stages are transcribed; no 68040 arithmetic | `m68040_iu_timing_suite` 99, `m68040_fpu_timing_suite` 32, `m68040_fp_pipeline_suite` 18 |
| Core-board registers (`010000`-`011600`) | working for the four that could be measured: CPU status (bit 15 stuck, writes clear the latched bits), CPU control and latch-page-on-parity (16 bits of storage), cache control (a *byte*, mirrored into both halves of a 16-bit read, one writable bit), each aliased across its 256-byte range. No manual here lays out these bits, so all of it is measured. **Width and storage only — no bit has a known meaning, and nothing may depend on one.** Task alias and master request are absent from the oracle and stay declined rather than modelled as all-ones | `boardreg_suite`, 12 tests; `FINDINGS.md` C10, `tools/mame-oracle/regprobe.lua`, two probe runs byte-identical |
| Address translation map (`017000`) | working: the translation itself, both DMA widths, and the register file. Between the AT bus and physical memory, not the CPU's MMU -- a DMA controller has no MMU, and this is what lets it see scattered physical pages as one contiguous run. Present on DN3500/4500/5500 and absent on DN3000, from the model table | `atmap_suite`, 15 tests, `019411-A00` §4.2.1.4, `008778-03` §1.2, §2.5 |
| Board cache (`012000` RAM, `014000` condition codes) | not started. The shared **bus arbitration point** is done and has its own row above | — |
| Apollo interrupt controllers (`011000`, `011100`) | working: the two 8259As cascaded on **IR3** (measured, not IR2 as the AT convention would have it), vector bases `A0`/`A8` from the boot PROM's own ICW2, giving levels `A0`-`AF`. Priority order matches `008778-03` Table 2-3, which with the cascade on IR3 has no anomaly. The CPU interrupt level is **6**, also measured — neither manual states it, and it took starting the interval timer by hand to make anything request at all | `intr_suite`, 13 tests; `FINDINGS.md` C11, `tools/mame-oracle/writetrace.lua` |
| Intel 8259A interrupt controller (the part) | working: ICW1-4 sequence, all three OCWs, fully nested priority with rotation, edge and level triggering, special mask and special fully nested modes, poll, AEOI, and the spurious level 7. 8086-mode vectoring only — MCS-80/85's `CALL` sequence is refused rather than approximated, and this machine never uses it. The Apollo *pairing* is a separate module | `i8259_suite`, 28 tests, each citing `8259A` 231468-003 |
| DN3500 core-board address map (`board/ap_board.c`) | working: every device placed by `008778-03` Table 2-8 and by the measurement that confirmed it, main memory at `1000000`, and an unclaimed address reported **unmapped rather than zero** — the distinction flat RAM hid, which cost 5634 invisible accesses in the first firmware run. Regions are named, so a trace can say *what* the firmware reached for | `board_suite`, 14 tests |
| Shared bus arbitration point | working: the external priority encoder `[030]` §7.7 requires, DRQ0 through DRQ7 with the processor last, driving the CPU's own arbitration unit over the three-wire protocol. A grant and its acknowledgement are separate instants, so the processor stops driving the bus when it grants rather than when the grant is taken up; a master is never pre-empted mid-transfer | `arbiter_suite`, 9 tests, `MC68030 User's Manual 3ed` §7.7, `008778-03` §2.4.6 |
| Apollo DMA controllers (`010C00`, `010D00`) | working: DMA 1 at **stride 1** and DMA 2 at **stride 2**, both measured, both aliased through their ranges. A read of a write-only register returns zero where the oracle returns `0F`; `[8237]` marks that read "Illegal", so neither is specified and ours does not invent a register value | `dma_suite`, 6 tests; `FINDINGS.md` C13 |
| Intel 8237A DMA controller (the part) | **programming model complete**: all sixteen register addresses, four channels with base and current address/count, the single shared first/last flip-flop, command/mode/request/mask/status/temporary, master clear, autoinitialise reload and the mask-on-terminal-count rule. Transfers themselves are **not** modelled and cannot be until there is a shared arbitration point to run a cycle on — a real dependency, not a scoping choice. Not yet wired to the board | `i8237_suite`, 18 tests, `8237A` 231466 |
| Apollo interval timer (`010800`) | working: the part at **odd addresses, stride 2** (measured — the region reads `00 00 00 00 00 FF ...`, the `FFFF` latch default showing through), the three §3.8 input rates as exact time-base clock domains, and the IRQ0 route. Advancing is by whole pulses, so the rate cannot become a function of how often it is polled | `timer_suite`, 8 tests; `FINDINGS.md` C12 |
| MC6840 interval timer (the part) | working for **both counting modes** — continuous and single shot, each in sixteen-bit and dual eight-bit operation — plus both control register aliases, the write/read byte buffering, the status register, the prescaler, the gate, and all five of `[6840]` §3.11's ways of clearing an interrupt. The two **measurement modes** are decoded and declined: they time a signal on a timer's gate pin, and nothing on this board drives the gates | `mc6840_suite`, 28 tests, `MC6840UM` (a scan with no text layer; read from page images) |
| Apollo calendar (`010900`) | working: **stride 1, byte consecutive** (measured — and not the timer's odd-address stride 2, so neither placement could be inferred from the other), sixty-four registers aliased through the 256-byte range, and the IRQ8 route through to vector `A8` | `calendar_suite`, 5 tests; `FINDINGS.md` C12 |
| MC146818A calendar (the part) | working: ten clock bytes, four registers, 50 RAM bytes, the once-per-second update with a full Gregorian carry, the alarm with don't-care codes, and Register C's read-to-clear. **Time is supplied by the caller, never the host** — the oracle seeds its calendar from the wall clock, which would rot every golden. The **periodic interrupt** is implemented for the nine rates that divide the time base (512 Hz to 2 Hz); the six fastest are refused rather than rounded, because `AP_TIME_BASE_HZ` factors as 2^9·3·5^8·11 and they need 2^15. Square wave and daylight-savings shifts are declined. Not yet wired to the board at `010900` | `mc146818_suite`, 26 tests, `MC146818A` (register figures read from page images) |
| Node ID PROM (`011200`) | working: the layout measured from the oracle's own PROM — stride 2 with the **odd byte reading zero** (unlike the serial ports at the same stride), the identifier big-endian in registers 0-3, and a checksum in register 14 confirmed arithmetically (`01 + 23 + 45 = 69`). The identifier is supplied by the caller, never a constant: a device whose purpose is to be unique per machine must not be the same on every one | `nodeid_suite`, 6 tests |
| Apollo serial ports (`010400`, `010500`) | working: both DUARTs at **stride 2** (measured), sixteen registers over thirty-two bytes and aliased, sharing IRQ1 through to vector `A1`. The memory-refresh square wave of §3.9 is pinned at exactly 99000 base units — its *frequency*, 66666.67 Hz, is not an integer, so a core counting in hertz could not represent this board's refresh clock at all | `sio_suite`, 10 tests; `FINDINGS.md` C14 |
| MC68681 / SCN2681 DUART (the part) | **programming model complete**: all sixteen register addresses of `[68681]` Table 4-1, both channels' mode registers with their shared pointer, clock-select, command and status registers, the three-deep receive FIFO with overrun, the interrupt status and mask registers, the input and output ports, and the counter/timer with both address-triggered commands. Serial framing itself — baud rates, start/stop bits, parity, the echo and loopback modes — is **not** modelled: a character is handed over whole. Not yet wired to the board | `mc68681_suite`, 34 tests, `MC68681 DUART Sep85` |
| QIC-02 tape drive | working for the readable half of the command set: both SELECTs with the sticky selection and the soft lock, BOT, RETENSION, SELECT Q24, READ and READ STATUS. **Writing is refused rather than discarded** — there is no write-back path, and accepting a write would let an installation appear to succeed. The cartridge *type* is supplied by the caller, because the controller derives it from tape geometry a raw image does not carry. The two opcodes the scan lost are claimed by nothing | `qic_suite`, 12 tests; `FINDINGS.md` C25 |
| Cartridge tape images (`image/ap_ct.c`) | working: block addressing over a raw `.ct` image, refusing any size that is not a whole number of 512-byte blocks, and boot-record parsing that returns the four header words. Their reading as load address and entry point is now **confirmed by the boot code itself** — its first instruction, a PC-relative `LEA`, computes word 0 exactly when executed at word 1, so the image proves its own layout. `ap_ct_boot_image` therefore *names* load address, entry point and length, and refuses a cartridge that does not announce itself, or whose header describes more than the file holds. Takes memory, never a filename, so `src/core` keeps its zero file I/O and the tests need no gitignored media | `ct_suite`, 12 tests; `FINDINGS.md` C24 |
| Apollo display controller identification (`05D800`, `05E800`) | working for **identification only**: both register blocks decode whether or not a screen is fitted, and the device ID at offset 1 reports `C4P=8`, `19I=9`, `C8P=10` or `15I=11` for the fitted family and `FF` for the other. An absent screen reads `FF` and does **not** bus error — "nothing is fitted" and "nothing is there" are different answers, and getting that wrong cost an investigation. Drawing, the blitter, the lookup table and the graphics memories are not modelled, and the header says so; unmodelled registers read `FF` rather than zero because zero is a value several of them can hold | `graphics_suite`, 14 tests; `FINDINGS.md` C31-C32 |
| Apollo cartridge tape (`050000`) | working, **controller joined to the drive**: a data-register write with the request bit set is a QIC-02 command, reads deliver the cartridge a byte at a time across the drive's block boundary, and a refused command or the end of tape raises Exception. The command handshake's **three entry conditions** are modelled — ready, exception, device-holds-the-bus, one figure each — as an *ordering*. Its timings are not: every figure in §1.13.2 publishes bounds rather than values, so modelling them means `PROVISIONAL` figures and that work is not done. The ordering is right about everything a polling driver observes. Four registers at stride 1, the upper four of each eight floating to `FF`, aliased through the range, on IRQ5 through to vector `A5`. The measured reset dump is reproduced over two aliasing periods | `tape_suite`, 14 tests; `FINDINGS.md` C16-C19 |
| Archive SC-499 cartridge tape controller (the part) | **register model complete**: all four addresses of `[SC499]` §1.9 — data/command, control-on-write and status-on-read, and the two write-triggered DMA commands — plus the derived interrupt flag, the tri-stated IRQ line, and RSTDMA's documented identity with power-on reset. The QIC-02 command set itself, tape motion and the drive behind it are not modelled. Not yet wired to the board at `050000` | `sc499_suite`, 13 tests, `Archive SC-499 Information Guide` | **Oracle note:** MAME's own SC-499 models no media change at all, so a cartridge swapped while Domain/OS holds the drive crashes it; `ext/mame` carries a local edit treating insertion as a QIC-02 RESET, per `FINDINGS.md` C56.
| Apollo disk and floppy (`04D000`, `05F800`) | working: both halves of the one card, placed **74 KB apart** by measurement, each aliased through 1 KB on its own period — four registers for the fixed disk, an eight-address block for the floppy. Interrupts on IRQ14 and IRQ6, separate lines eight apart. The gap is pinned as arithmetic, not constants: the AT window maps `Apollo = 0x040000 + AT × 0x80` | `disk_suite`, 6 tests; `FINDINGS.md` C20, C22, C23 |
| OMTI command descriptor blocks | working: the 6-byte CDB decoded with the **cylinder reassembled from three bytes** (C10 in byte 1, C09/C08 in byte 2, low eight in byte 3), the command byte exposed both whole and split into class and opcode, and acceptance checked against the ESDI command set — which **refuses** `0C INITIALIZE DRIVE CHARACTERISTICS`, an ST506-only command that would make ESDI geometry look settable | `omti_cdb_suite`, 7 tests; `FINDINGS.md` C27 |
| OMTI 862X ESDI/floppy controller (the part) | **register model complete for both halves**: the fixed disk's four ports with their read/write asymmetries and the status register's fixed bits, and the floppy's five at the standard PC layout. Modelled as two independent register sets sharing nothing, as `[OMTI]` §4.1 and §3.4 describe. Both measured dumps reproduced as tests. The **command sets** (§5, §6) are not modelled — they want a drive and a disk image. Not yet wired to the board | `omti_suite`, 9 tests, `OMTI AT Controller Series Jan87` |
| OMTI 8621 placement (the DN3500's disk) | measured, both halves. Placement characterised at `04D000`: the range is the card's (all `FF` without it, control verified by device enumeration), aliased on an eight-byte period, with offsets 1-3 driven. Offsets 0 and 4-7 read `FF`, which a read sweep cannot distinguish from undriven | `FINDINGS.md` C20 |
| WD7000 ESDI/SCSI (DN4500) | not started | — |
| Floppy, QIC cartridge tape | not started | — |
| Mono and colour graphics controllers | not started | — |
| 3c505 802.3 Ethernet | not started | — |
| MAME oracle harness | working and used throughout. Beyond the dumper there are now four probe tools — `regprobe.lua` drives every bit of a register in both directions, `writetrace.lua` taps writes to watch firmware program a device, `steptime.lua` single-steps for instruction timing, `mdcapture.lua` traces the serial registers byte-exact — and findings C10 through C14 are all measurements taken with them | `oracle_driver` (19 checks, stub MAME) and `oracle_dump_format` (19 checks, mock machine); `./apollo -listfull` lists all eleven apollo machines |
| Interactive boot-PROM session (`mdsession.py`, `mdsession.lua`) | working, and it performed the Domain/OS install end to end. Holds a machine open across stages, reads the console and answers it; stdin is a **pty**, so a command is written when its prompt appears rather than trickled at a fixed rate. `--commands FILE` is followed while the run continues, so an unpublished dialogue can be answered as it is read. `!swap` changes a cartridge without stopping the machine. A killed driver takes its emulator with it. **Deliberately not reproducible in the oracle-reading sense**: it is paced by the host, so nothing timed may be measured through it — its products are a disk image and a transcript | `oracle_session`, 31 checks against a stub MAME that goes deaf on `re` as the real machine does; `FINDINGS.md` C49-C58 |
| Golden regression harness | working | `golden_model_table`, run under every build preset; drift, `-O3` identity and regeneration all verified |
| Shared frontend layer (`frontend/common/`) | working | `frontend_common_suite`, 10 tests |
| Headless frontend | `--model`, `--list-models`, `--help` | `golden_model_table`, which supersedes the old smoke test |
| SDL frontend | not started, deliberately not stubbed | — |

## What is established, and from where

Findings that already have a cited source, so they are not re-derived later.

### Oracle

- MAME's `apollo` driver (`ext/mame/src/mame/apollo/`) covers `dn3000`,
  `dn3500`, `dn5500` and the `dsp*` server variants, and boots Domain/OS. It is
  the runnable oracle: **built and instrumented, never linked.** GPL-2.0-or-later
  against this core's MIT.
- MAME's Domain networking is **802.3 over an emulated 3c505**, not the Apollo
  Token Ring. There is therefore no runnable reference for the ring at all.
- MAME does **not** model DN2500 or DN4500. Those two are paper-only.
- Known MAME limitations, per its own driver notes: some FPU operations and
  operands, Winchester bad-track formatting, and certain video synchronisation.
  Expect to out-accurate the oracle in those areas and record it as a class.

### The probe injection path

- The DN3500 boot PROM holds the **Mnemonic Debugger** (`008778-03` §1.5.1), an
  interactive monitor with `A` (access/deposit), `G` (jump), `D` (dump memory),
  `DR` (dump registers), `SS` (single step) and `B`/`CB` (breakpoints). Full
  command set in `docs/references/MD.md`, from `002398-04` ch. 5.
- This is how probes will be injected, and it removes three obstacles at once:
  no cross toolchain, no need to recover Apollo's on-disk executable format
  first, and no Domain/OS boot — MD runs from PROM before any OS. That is what
  keeps the probe suite a Phase 1 deliverable rather than one gated on Phase 4
  storage work.
- Two commands are worth noting early: `TE` runs the boot PROM's own
  diagnostics — the hardware's test suite for free, the same free-test argument
  as the ring firmware's self-test — and `IC` toggles the instruction cache,
  which is what makes a cache-effect timing probe possible at all.
- **Input syntax: closed, and from the manual rather than the oracle.** This was
  recorded as open on the grounds that the syntax "is not in the handbook's
  command list" — true, but the handbook continues *past* the list into a
  per-command reference (`002398-04` pp. 5-7 on) and then states the grammar
  formally at pp. 5-13/5-14. Now transcribed in `docs/references/MD.md`: the
  full BNF, hexadecimal-by-default input, `<size_spec>`/`<base_spec>` placement,
  `*` as current location, and the `AR` control-register names
  (`TC`/`RP`/`DFC`/`SFC`/`CACR`/`CAAR`) that are the Phase 2 MMU and cache probe
  surface. The scan's OCR mangles `|` and `::=`; that is called out in the file,
  and the reconstruction rests on the handbook expanding every token in prose
  directly below the grammar, not on inference.
- **Still open: the output format.** The handbook never shows a literal output
  line, so column layout, separators, prompt and terminator are unknown — and
  the harness has to parse exactly those bytes. That still wants a captured
  session under the oracle. The no-guessing rule governs the parser; it no
  longer blocks the encoder's input side.

### 68030 instruction timing, and why the tables are a check and not a recipe

Recorded in full in `docs/references/M68030_TIMING.md`, from `MC68030 User's
Manual` 3ed ch. 11. The load-bearing fact:

**No published average-no-cache-case number is a value any single execution of
that instruction ever takes.** Motorola computed the odd-word-aligned and
even-word-aligned cases and published *the mean, rounded up* (§11.3.3, p. 11-8),
and the same for prefetch bus-cycle counts. The cache-case figures separately
assume no overlap, no data-cache hits, and two-clock bus cycles throughout.

So an emulator that looks up an instruction's published cycle count and adds it
is not cycle-accurate and cannot be made so by refining the table — it is
reproducing an average the hardware never exhibits on any particular run. This
is the concrete justification for the strictly cycle-stepped reference core:
alignment, cache state, wait states and contention all fall out of the machine
rather than being tabulated, and ch. 11 then serves as an independent check on
numbers we produce.

Nothing from ch. 11 is in code. This is reference only.

### Why the constructed machine and the probes are shaped as they are

Moved here from the plan, which now states the items and points at this.

**The machine** (`ap_machine.c`) is a 68030 on flat RAM and nothing else, built
*before* the boot-PROM route rather than after it, on the evidence of
`FINDINGS.md` C4. It is deliberately not a DN3500: no I/O, no device, no
arbitration point, because a probe harness that had to be a whole machine would
be a worse probe harness, and machine variance belongs in the model table.

- **RAM is supplied, not allocated.** The core allocates nothing, so a probe
  picks its own size and a test can put one on the stack.
- **Outside the RAM is a bus error, counted rather than merely refused.**
  Wrapping would invent an alias the hardware does not have; reading zero would
  make an out-of-range probe look like a working one that found empty memory.
  The range is checked as a *range*, so a long word straddling the top is
  refused rather than reading past the buffer.
- **A run takes a limit**, because a probe that loops forever must end as a
  failed probe rather than as a hung harness.

**The probes** (`ap_probe.c`) follow one rule: *a probe reports rather than
judges*. Nothing in the module knows what any result should be. A unit test
asserts what someone expected; a golden pins what the emulator did, byte for
byte, on every platform and both build types — which is the cross-platform
identity claim, and the only mechanism that catches one platform quietly
disagreeing with three.

**Every probe ends with `STOP`**, so it finishes because its program said so
rather than because it ran out of limit. Two were built without a terminator and
their first goldens showed it: a loop reporting 20 instructions for six
iterations of work, and a subroutine that returned and then fell into its own
callee. A probe that hits its limit reports whatever it happened to be doing.

The runner blanks the RAM and plants a returning handler on every vector between
probes, so a result cannot depend on what ran before it and an unexpected fault
lands somewhere legible instead of in blank memory. The reported clock is bus
and cache time only, and says so in the report; it is pinned anyway, so that
when instruction execution time arrives the diff says by how much for every
probe at once.

### 68030 MMU instruction encoding: five traps that do not fault

Moved from the plan. Every one of these produces a *plausible* wrong answer —
nothing raises an exception, so the mistake surfaces later as a translation
going somewhere strange.

- **`PFLUSH` and `PLOAD` share the extension prefix `001`** and are told apart
  by the MODE field below it: PFLUSH is `001`, `100`, `110`; PLOAD is `000`. A
  decoder stopping at the prefix does the *opposite* of what was asked — PLOAD
  adds a translation where PFLUSH removes one.
- **The flush mask says which bits must agree, not which codes to flush.** "Each
  bit in the mask that is set to one indicates that the corresponding bit of the
  FC operand applies", so a *zero* mask selects every function code rather than
  none. Read the other way, `PFLUSH #0,#0` becomes a no-op where the hardware
  flushes everything.
- **The FC field is not a plain number**: `10XXX` is an immediate code, `01DDD`
  is *data register DDD's* low three bits, `00000` is SFC and `00001` DFC.
  Reading the low three bits directly makes `01DDD` name a function code that
  happens to be the register number — for D5 that is 5, an ordinary supervisor
  data code, so nothing looks wrong.
- **`PFLUSH` and `PLOAD` use the calculated address as the operand**, never
  reading through it: the address field "must provide the memory management unit
  with the effective address to be flushed ... not the effective address
  describing where the PFLUSH operand is located".
- **`PMOVE`'s three formats are told apart by the extension word's top three
  bits**, and P-REGISTER is not enough on its own: `010` names the *supervisor
  root pointer* under prefix `010` and *TT0* under prefix `000`. A decoder
  reading only P-REGISTER writes a transparent translation register where a root
  pointer belongs, and both hold plausible 32-bit values.

Three consequences worth keeping:

- **Two writes fault *after* landing.** An invalid root pointer descriptor type
  and an inconsistent TC both take an MMU configuration exception "after moving
  the operand", and TC additionally has its E bit cleared. Refusing the write
  instead would leave the operating system unable to see what it wrote wrong.
- **`PTEST` at level 0 probes the ATC; levels 1-7 walk the tree with a NULL
  history-update callback** — which is what `ap_m68030_walk`'s nullable `update`
  exists for — so it alters neither the ATC nor the tree, and is therefore usable
  inside a fault handler without changing the state being diagnosed. A level-0
  `PTEST` with the A bit set is an F-line exception, since an ATC probe never
  fetched a descriptor whose address it could return.
- **The FD bit makes `PMOVEFD` a different instruction in one bit** — "if the FD
  bit equals one, the ATC is not flushed" — and the status register's format
  carries no FD bit at all, so flushing is not something a write to it does.

The MMU registers live on the CPU rather than in whatever the caller supplied,
because there is one MMU and two access paths through it. The root pointers are
unpacked by the *walk's* own long-descriptor code, so the two cannot drift
apart.

### 68030 instruction semantics that would otherwise be silently wrong

Moved from the plan, which now states the items and points here. Each of these
produces a plausible result rather than a fault, so nothing catches it at the
time.

**`MOVEM`'s mask is reversed for predecrement** — "bit 0 A7 … bit 15 D0" against
"bit 0 D0 … bit 15 A7" — which lets one loop over bits 0-15 give both documented
orders. Reading it the same way round for both saves the right number of
registers into the right amount of space with every one in the wrong place, and
a matching postincrement `MOVEM` restores them anyway, so only an outside
observer of memory can see it. Two more `MOVEM` facts are this part's rather
than the 68000's: a word transfer **sign-extends into the whole register**, data
registers included, unlike every other data register write; and if the address
register is itself moved to memory, the value written is the *decremented* one,
where the 68000 and 68010 write the initial value.

**`CHK`'s comparisons are signed** — "the upper bound is a twos complement
integer" — so an unsigned model lets a negative register pass any bound whose
top bit is clear, which is nearly every bound anyone writes.

**`TAS` flags the value before setting the bit.** The other order makes it always
report an already-taken semaphore, and everything built on it deadlocks.

**`MOVE from SR` is privileged and `MOVE from CCR` is not**, which reads
backwards from the 68000 and is why it is stated rather than assumed.

**`MOVEC`'s control register codes are not a dense index**: bit 11 separates the
68010's SFC/DFC/USP/VBR from the 68020's CACR/CAAR/MSP/ISP, so `$800` is not
`$002` with a different index. A model treating the field as a small number puts
the USP where CACR belongs, and both hold plausible 32-bit values. An undefined
code is an illegal instruction rather than a silent no-op, which is how the
68040-only MMU codes stay out — and this is what makes `VBR` and `CACR`
reachable at all, both of which the boot PROM sets.

**`STOP` loads the status register first**, interrupt mask included: that is the
whole point of the instruction, and loading the mask after halting would leave a
window at the old priority. A stopped processor does not prefetch, so the step
returns before touching the pipe.

**`RESET` changes nothing inside the processor** — "the processor state, other
than the program counter, is unaffected, and execution continues with the next
instruction" — so it is counted rather than acted on. A model that halted or
reset itself here would stop the boot PROM at its first line, since resetting
the devices is among the first things it does.

**An untaken wide branch still consumes its displacement words.** The read has to
happen before the condition is tested, or the PC lands inside the displacement
and executes it as an instruction.

### Golden result blocks

- A *result block* is any deterministic report the emulator prints. Each is
  pinned by a committed file under `tests/goldens/` and checked by
  `tools/regress.py`; `cmake/Goldens.cmake` registers each as an ordinary CTest
  entry labelled `golden`, so it runs under every build preset on every
  platform.
- **Why a committed golden and not a diff of two builds.** Comparing `-O0`
  output with `-O3` output proves only that the two agree, which is also true
  when both have drifted together. Checking each against a reviewed file catches
  that, and catches one platform quietly differing from the other three — which
  is the property this project actually claims. The CI identity job's hand-rolled
  `diff` was removed in favour of this.
- Goldens are regenerated by the `goldens-update` build target, never edited by
  hand, so a golden cannot be quietly adjusted into agreement with a change. A
  mismatch prints a unified diff and says to commit the regenerated golden
  *alongside* the change that caused it, because a golden updated in its own
  commit hides what moved.
- Absent Python 3 is a **fatal configure**, not a skipped test; `-DAPOLLO_GOLDENS=OFF`
  is the explicit opt-out. A green `ctest` that pinned nothing is worse than a
  failed configure.
- *Verification: verified four ways — a perturbed golden fails with a named
  diff, restoring it passes, the release build matches the same golden as the
  `-O0` build, and `goldens-update` regenerates byte-identically.*
- Only one block exists today (`model_table.txt`, from `--list-models`), because
  only the model table produces one. Probe results, register dumps and boot
  state hashes join it as those subsystems land.

### Supported platforms and toolchain

- Three platforms, all 64-bit: **Linux x86-64** (Red Hat and Debian derived),
  **Windows x86-64** (clang in an MSVC environment), **macOS arm64**. Clang is
  the default and only supported compiler; all nine presets set it.
- 64-bit is **enforced**, not assumed: `cmake/Platform.cmake` fails the configure
  on a 32-bit target. Time is a `uint64_t` in `AP_TIME_BASE_HZ` units, so a
  32-bit build is a silent-wrong-answer risk rather than merely untested. A
  non-Clang compiler or an unlisted platform warns rather than fails — useful as
  a portability check, but off the supported path.
- *Verification: the configure prints the resolved triple and compiler
  (`apollo: Linux/x86_64, Clang 21.1.8, 64-bit`); the CI matrix builds all three
  platforms plus the `-O0` vs `-O3` identity job.*
- **`-Werror` in every build type, not debug and CI alone.** In an emulator a
  warning is rarely cosmetic: `-Wconversion` and `-Wsign-conversion` fire on
  exactly the silent width and signedness changes that make a cycle count or an
  address wrap differ between platforms, which is the one thing this project
  claims cannot happen. A warning that is an error in Debug and a note in
  Release is a warning that gets through in Release, so `APOLLO_WERROR` defaults
  to `ON` and `release-base` sets it explicitly. The warning set is applied by
  `apollo_set_warnings()`, which is never called on anything in `ext/`, so
  vendored code is unaffected.

### Third-party dependencies

- Six pinned submodules, documented per-submodule in `ext/README.md`. Four are
  linked — `unity` `v2.7.0`, `zlib` `v1.3.2`, `libpng` `v1.6.58`, `sdl`
  `release-3.4.12` — and two, `mame` and `musashi`, are reference-only and never
  enter a build.
- All four linked submodules now sit on **release tags**. They had been added at
  whatever branch tip was current — zlib on `develop`, libpng on the `libpng18`
  development branch, SDL on `main` — which records a stable SHA but names an
  arbitrary mid-development commit upstream never released or tested as a unit.
  A project whose premise is bit-identical output across platforms and build
  types cannot rest on those.
- **Only `ext/unity` is needed to build.** zlib, libpng and SDL are declared so
  the versions are recorded, but no target references them until the media and
  display phases. *Verification: a fresh clone with only `ext/unity` initialised
  configures, builds and passes `ctest`; CI's `CI_SUBMODULES` does the same on
  all four platforms.*
- The MIT/GPL boundary is **asserted at configure time**, not left to review:
  `cmake/GplBoundary.cmake` fails the configure if any first-party source
  includes a `mame` or `musashi` header, or if any target acquires one in its
  link libraries or include directories. *Verification: both halves were probed
  with a deliberate violation and both fail with a named message.*

### Address map (Series 3000/4000, `008778-03` Table 2-8, Table 2-9)

| Range | Device |
| --- | --- |
| `0x000000`–`0x00FFFF` | boot PROM |
| `0x010000` / `0x010100` | CPU status / control register |
| `0x010200` | cache control register |
| `0x010300` | task alias register |
| `0x010400` / `0x010500` | SIO1 / SIO2 |
| `0x010800` / `0x010900` | interval timer / calendar |
| `0x010C00` / `0x010D00` | DMA controller 1 / 2 |
| `0x011000` / `0x011100` | interrupt controller 1 / 2 |
| `0x011200` | network ID PROM |
| `0x011300` | latch-page-on-parity-error register |
| `0x011600` | master request register |
| `0x012000` / `0x014000` | cache RAM / cache condition-code RAM |
| `0x016400` | selective clear locations |
| `0x017000` | address translation map |
| `0x040000`–`0x05FFFF` | AT-compatible bus I/O space |
| `0x051000` (AT `0x220`–`0x23F`) | **Apollo Token Ring controller** |
| `0x058000` (AT `0x300`–`0x310`) | 802.3 Network Controller-AT |
| `0x059000` (AT `0x320`–`0x33F`) | second ring controller |
| `0x04D000` (AT `0x1A0`) | Winchester |
| `0x050000` (AT `0x218`) | tape drive |
| `0x05F800` (AT `0x3F0`) | floppy interface |
| `0x080000`–`0x09FFFF` | AT-compatible bus memory space |
| `0x1000000`+ | main memory |

### Firmware in hand

All from bitsavers `bits/Apollo/firmware/`, gitignored locally under
`roms/firmware/`.

| Image | Size | Notes |
| --- | --- | --- |
| `2500_BOOT_16182_8` | 128 K | only DN2500 artefact we have |
| `3000_BOOT_8475_4`, `_7` | 32 K | two revisions |
| `3500_BOOT_12191_7` | 64 K | reference superset boot PROM |
| `4500_BOOT_13167_02_MD7R.0.32` | 64 K | |
| `5500_BOOT_A1631-80046` | 64 K | |
| `3000_RING_1818-4882` | 8 K | ring firmware, rev 4.0 |
| `3500_RING_10666_6` | 8 K | ring firmware, rev 3.6 |
| `4500_RING_10666_8` | 8 K | |
| `5500_RING_1818-4882_R9` | 8 K | |
| `3000_3C505_010728-00`, `3c505.zip` | 8 K | 802.3 controller |
| `3000_OMTI_8621`, `4500_WD7000_ESDI`, `_SCSI` | 16–32 K | disk controllers |
| `3500_TAPE_80234-003` | 8 K | QIC tape |
| `4500_Matrox_013748_04` | 64 K | DN4500 graphics |
| `3500_NI_1C874` | 32 B | node ID PROM |

Two ring board generations are visible: part `10666` on the 3500/4500 and HP
part `1818-4882` on the 3000/5500. Both identify as
`Apollo Token Ring Network Controller-AT`.

### Media in hand

| Item | What it is |
| --- | --- |
| `Apollo_DOMAINOS_SR10.3.5.tgz` | 5 tapes, 176 numbered `.img` files |
| `019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct` | bootable QIC boot cartridge |
| `019594-001..004.CRTG_STD_SFW_{1..4}.ct` | QIC install cartridges 1-4 |
| `cptape.hlp` | Apollo `cptape` help text, rev 9.0, 1986-12-17 |
| `dn3500-sr10.4-installed.awd` | **built here**: an installed, bootable system |

The `.img` files are **tape files, not disk images** — one file per tape mark,
the layout `cptape` writes. `tape1/00.img` is 8192 bytes beginning
`00 13 d8 00 … "SYSBOOT REV" … " M68K    "` followed by 68000 code: the Apollo
tape boot record.

All of it except the last row is *installation* media. The last row is what this
project made from it, and the section below says how. Everything here is
gitignored; the built image is pinned instead by
`docs/references/DOMAINOS_IMAGE.md`.

### Model figures confirmed from `[CFG]`

Cited as `[CFG]` = HP-Apollo Products Configuration Guide, Dec 1989.

| Model | Confirmed |
| --- | --- |
| DN2500 | MC68030 @ 20 MHz + MC68882 @ 20 MHz, on-board monochrome graphics, SCSI bus (7 devices), 3 async RS232 ports, 4–16 MB RAM, 15" mono 1024×800 or 19" mono 1280×1024 |
| DN3500 / DN3550 | MC68030 @ 25 MHz + MC68882, 4–32 MB RAM |
| DN4500 / DSP4500 | MC68030 @ 33 MHz + MC68882 @ 33 MHz, 4–32 MB RAM, 7-slot AT/XT bus (6 AT, 1 XT) |
| DN10000 | PRISM @ 18.2 MHz, up to 4 CPUs, dual 64-bit FPUs per CPU, 8–128 MB RAM — recorded only to confirm it is a different machine and out of scope |

### Time base

`AP_TIME_BASE_HZ = 6.6 GHz = LCM(12, 20, 24, 25, 33 MHz)`, giving exact integer
periods: 550 units at 12 MHz, 330 at 20 MHz, 275 at 24 MHz, 264 at 25 MHz, 200
at 33 MHz, and a 12 Mbit/s ring bit cell of 550 units built from two 275-unit
bi-phase windows. This is a *derived* constant — adding a clock it does not
divide means recomputing the LCM, which changes the unit and no emulated
behaviour.

The base was 3.3 GHz until the ring's second clock domain was confirmed. The
Apollo ring PHY is bi-phase encoded, so 12 Mbit/s is the data rate while the
line clock is 24 MHz (`010005-00` §3.2 p.3-3, recorded as findings 10/10a in
`docs/references/RING.md`) — and 3.3 GHz divides 24 MHz only as 137.5. Doubling
the base restored exactness. This is the discipline working as designed rather
than a correction: the constant is derived, `ap_clock_init()` rejects a
frequency the base cannot represent instead of rounding it, and every period is
computed from the base rather than written down, so no emulated behaviour moved.
A video dot clock is the next candidate to force a recomputation.

### A manual figure that did not survive its scan

`MC68030 User's Manual` 3ed Figure 9-37 gives the bit layout of the transparent
translation registers. Its lower half — the positions of E, CI, R/W, RWM,
FC BASE and FC MASK — OCRs to nothing but a stray "FC MASK", identically under
`pdftotext -layout` and plain extraction, so the loss is in the scan rather than
the extraction. The upper half is legible (31-24 logical address base, 23-16
logical address mask), and every field's *meaning* is given in prose.

`src/core/cpu/m68030/ap_m68030_tt.c` therefore models the register as **decoded
fields** and implements the documented semantics, leaving the packing undone
rather than inferred from a plausible-looking layout. Nothing needs the packing
until software writes the register with `PMOVE`. Recorded as a named item in
`docs/COMPLETION_PLAN.md` with two ways to close it.

This is the same discipline as the MD grammar, where the OCR damage *was*
recoverable because the manual expanded every token in prose below the figure.
Here it is not, so it stays open.

## Deliberate approximations

Each carries its reason and cost to close. Distinct from the `PROVISIONAL`
figures below: those are *numbers* modelled from a documented bound where no
measurement exists, these are *behaviours* knowingly modelled differently from
the hardware.

| Approximation | What it does instead | Why | Cost to close |
| --- | --- | --- | --- |
| 68030 `RTE` from a bus fault frame | **Re-executes** the faulted instruction from the start rather than resuming mid-instruction | The real part resumes from the internal registers it saved, and this model has none to save | Needs the long frame's internal registers, which need a microsequencer model. Exact meanwhile when the faulted access precedes any side effect — every case the boot PROM reaches — and wrong for an instruction that had already committed one |

## PROVISIONAL figures

Every entry is also a named item in `docs/COMPLETION_PLAN.md`, and every
`PROVISIONAL` **figure** in the source is a row here. Audited in both
directions: each table row has a plan item, and each plan item points back here.

The word "figure" is doing work. Three source files say `PROVISIONAL` without
naming one — `ap_time.h` points at the model clocks, and `ap_frontend.c/.h`
*print* the marker for whichever models carry it — so a grep for the word
returns more sites than this table has rows, and always will. What must be here
is every quantity that was chosen rather than transcribed.

**This claim was false when it was first written**, and stood for some time: the
68882's version number, its idle state frame's internal words and the AT map's
entry indexing were all `PROVISIONAL` in the source and none was a row, and the
second of those was not a named plan item either — the silent deferral this
discipline exists to prevent. An audit claim is worth exactly as much as the last
time someone ran it, which is the argument for the count below rather than the
adjective above.

At the time of writing: **18 rows, each with a plan item, and no `PROVISIONAL`
figure in `src/` outside them.**

The plan names several of them in its own words rather than this table's — the
MC146818A rates appear as "whether to recompute the time base to admit the six
fast rates", the SC-499 handshake as its `§1.13.2` figures — so a literal search
for a row's title finds nothing and means nothing. Check the concept, not the
phrase.

| Figure | Current value | Why provisional | Cost to close |
| --- | --- | --- | --- |
| 68030 ATC replacement algorithm | first-invalid, then first entry with a clear history bit, sweeping when all are set | **Narrowed twice, and now down to a tie-break.** `MC68851 PMMU User's Manual` §5.2.1.3, describing the compatible ATC, states the algorithm outright: "locate an invalid entry and use it. If no invalid entries are found, use a psuedo least-recently-used (LRU) algorithm to select an entry without its L bit set and replace that entry." So the *order* -- invalid first, history bit second -- is documented rather than inferred, and the same section says the history bit indicates "that the entry has been recently used", which is why a translating hit marks it. The 68851's L bit has no counterpart on the 68030, which cannot lock an entry, so that exclusion drops out rather than being modelled as always false. What remains unstated in both manuals is only which entry is chosen *among those whose history bit is clear*. The `MC68030 Data Sheet 1991` is less specific still — "a variation of the least recently used algorithm" — and is a dead end rather than a lead | Measure eviction order against the oracle, or find a Motorola application note stating the rule; medium. Affects hit rates and therefore timing, never the translation a hit produces |
| 68030 data-dependent instruction times (the `+` rows of §11.6) | The published figure, used as a point value: `DIVU.W` 44, `DIVS.W` 56, `DIVU.L` 78, `DIVS.L` 90 clocks | §11.6.8's footnote is explicit that these are not values: "+ Indicates Maximum Time (Actual time is data dependent)". A divide's real cost depends on its operand pair, so a core using the maximum is slow by a data-dependent amount rather than wrong by a fixed one -- which is harder to notice, since every figure looks plausible and nothing ever disagrees by a constant. `CLAUDE.md` names exactly this case: model the documented value, mark it, and never invent a point number. The rows carry `data_dependent` and `timing_table_suite` asserts that **only** the four divides do, so a marker applied too widely would make every figure look provisional | Measure per operand pair against the oracle, or find a Motorola application note giving the algorithm's iteration count. Medium: the harness exists (`steptime.lua` side-loads and steps), but a divide's cost needs sweeping operand pairs rather than one reading. Affects only how long a divide takes, never its result |
| 68030 `+` rows not yet transcribed | absent, so the instructions are unpriced rather than priced wrongly | §11.6 marks more instructions data-dependent than the four divides: the four multiplies, `CMP2`, `CHK2`, `CHK` with the exception taken, and `CAS2`. None is in the transcription -- the multiplies and `CMP2`/`CHK2` are footnoted for an effective address time as well, and `CAS`/`CAS2` do not execute at all. `ap_m68030_timing_for_word` returns NULL for them, so the step leaves them at bus time alone, which reads as an alternating figure and is visibly a lower bound | Transcribe them with the same `data_dependent` marker once the instructions they belong to are priced. Small, and blocked on nothing except its own turn |
| 68030 `ABCD`/`SBCD`/`NBCD` `N` and `V`, and `CHK`'s `Z`/`V`/`C` | The rules real silicon follows. `N` is bit 7 of the result. `V` is the binary overflow between the **unadjusted** and corrected results -- set when the MSB goes 0→1 for `ABCD` and 1→0 for `SBCD`/`NBCD`. `CHK` sets `Z` from the *register* operand being zero and always clears `V` and `C` | Every manual consulted says only "undefined": the `M68000 Family PRM`, the perihelion instruction-set reference and prb28's instruction documentation all mark `N` and `V` as `U`. Undefined licenses *software* not to depend on them; it does not license a reference core to be non-deterministic, so the choice is between a cited rule and an invented one. The rules here come from an exhaustive hardware sweep -- every input permutation against several initial CCR states, fitted to 100% of cases -- cross-checked against Motorola's patent US4325121 for the BCD correction hardware, and since adopted by MAME, WinUAE, Hatari and BlastEm. **One reading had to be settled between sources**: the sweep's notation `(dd & ~rr)` reads as the destination, while the write-up states it as the MSB *changing*, which can only be unadjusted against adjusted. `NBCD` decides it -- it is the same subtract from a destination of zero, so a destination-based `V` could never be set at all | Run the same sweep against a **68030**. The sweep was on a 68000; the correction hardware is the family's and so is the patent, so the rule is very likely to carry, but likely is not measured. Small: the oracle steps side-loaded instructions already. Affects only flags the manual says nothing may depend on |
| 68882 transcendental functions | **All nineteen are implemented**, to within §4.3.2's published bound: worst measured error under 3.1 units in the last place of extended precision against expectations generated to 120 decimal digits, which is some twenty times inside the typical bound of 64 and three orders of magnitude inside the worst case of 4096. Two things remain provisional and neither is the accuracy. **(a) Bit-exact agreement with the part is not attainable**, because Motorola publishes a bound and no algorithm -- so our results differ from the hardware's in the low bits of almost every value, within the interval the manual guarantees. The one place this is *visible* rather than hidden in the last bits is the case §4.3.2 names itself: `FTENTOX #1` returns exactly 10.0 here and the manual says the hardware's does not. **(b) Directed rounding at extended precision is a no-op**, because this model computes a 64-bit approximation directly and has no bits below the destination left to round, where the part carries 67 internally; at single and double precision all four modes work correctly | §4.3.2 is explicit that there is nothing to transcribe: "the IEEE specification does not define the error bound to which transcendental (**except square root**) functions are to be performed", and Motorola publishes no algorithm -- only bounds. "The worst-case accuracy of any transcendental function is one unit in the last place of double precision (which is equal to 4096 units in the last place of extended precision). The typical error bound ... is approximately 64 units in the last place of extended precision." So a correctly-rounded implementation would be *more* accurate than the part and would differ from it in the low bits of almost every result -- which for a reference core is a divergence, not an improvement. Reporting unimplemented keeps that visible; approximating would hide it | **(a)** Only a 68881 ROM disassembly recovers the actual algorithm; every software route -- Motorola's own `M68040FPSP` included -- meets the same bound by a different recurrence and so cannot be bit-exact, which is the finding recorded at length in the middle column. **(b)** Carry guard bits through every kernel and round once from them. Bounded by one unit in the last place, so a sixty-fourth of the typical bound -- and the part's own directed rounding of a transcendental is accurate only to that same bound, so the gain is smaller than it looks |
| 68030 stack frame INTERNAL REGISTER words | written as zero, in the bus fault frames (`$A`/`$B`) and now in the coprocessor mid-instruction frame (`$9`) | Table 8-6 names the fields but gives them no defined contents — they hold microsequencer state, and this model has none to save. Written rather than skipped, because a frame that left them holding whatever the stack already had would hand a handler the previous program's data under a documented field name. Zero is a stated value; a skipped word is an unstated one | Only a microsequencer model closes it. What it costs today is bounded and named: an `RTE` from format `$9` is declined rather than resumed, since resuming is exactly what those words are for |
| 68851 root pointer table | **Not implemented.** §5.3's eight-entry cache of recently-used `CRP` values with a task alias each. A `PMOVE` to `CRP` takes the conservative branch instead: it flushes the current task's ATC entries and sets `PCSR`'s `F`, which is what a replacement would do when the table holds one live alias | The RPT is a *performance* mechanism -- "the root pointer caching and task alias maintenance performed by the RPT allows translation descriptors for multiple tasks to reside in the ATC simultaneously". Without it every task switch flushes, which is slower than the hardware and never wrong: the entries flushed are exactly those a real replacement could have invalidated, plus some it might have kept. No translation returns a different physical address; only the hit rate differs | Implement §5.3's eight-entry RPT with its own replacement, and let the task alias vary. Needed before any ATC hit-rate measurement is meaningful, and before a probe could compare ATC occupancy against the oracle. Does not affect correctness of any single translation |
| 68851 `U` and `M` write-back | **Implemented and wired.** A search returns the path of descriptors it read; `ap_m68851_status_writes` produces the byte cycles §5.1.5.3.11's table calls for; and `ap_m68851_translate` and `ap_m68851_pload` drive them through a `store` callback so the bits reach memory. The status byte is the fourth of the descriptor in both formats -- `U` is bit 35 of a long descriptor and bit 3 of a short one, `M` bit 36 and bit 4, and in each case that is bit 3 and bit 4 of `address + 3` | No longer provisional. Four readings, each sourced. **A descriptor already carrying the right bits produces no cycle**: the part "only performs write cycles to modify these bits are required". **The cycle type is specification**: a read-modify-write "whenever it is required to set the used bit but not affect the state of the modified bit", so two MMUs sharing a tree cannot lose each other's `M`; pointer descriptors, which "do not contain modified bits, are not referenced using read-modify-write sequences". **The path survives a fault** -- "a pointer may be fetched, and its U bit set, for an address to which access is denied at another level of the tree". And **`PTEST` must not write at all**: "U and M bits in the translation table are not modified by this instruction", where `PLOADW` updates "as if a write operation to that address had occurred" and `PLOADR` as if a read | Closed. `ap_m68851_ptest` takes no `store` parameter, so a probe *cannot* mark a page used -- the omission is how the manual's rule is enforced rather than merely documented. Two judgements remain readings rather than quotations: the descriptor that *causes* a denial is not marked (the manual covers the pointers above it and is silent on it, and `U` exists for page replacement, which an invalid descriptor has no part in), and a `NULL` store leaves the tables unchanged for a caller with no write path |
| SC-499 command handshake timings | the documented bounds | `[SC499]` §1.13.2 publishes *bounds*, not values — "0 us < T3->T4 < 150 us" says the device hands the bus back within 150 microseconds and nothing about when. Modelled at the bound, so every handshake runs at its slowest permitted speed: wrong in a knowable direction and by a knowable amount. All nine convert exactly to base units, so none is rounded on top of being provisional | Measure edge timings against a running drive, which needs the oracle's tape path exercised; small. Affects only a driver watching for the edges themselves — a polling driver cannot observe the difference |
| 68030 asynchronous input synchroniser | two clocks, giving a three-clock `BR`-to-`BG` grant latency | `[030]` §7.7.4 publishes a bound and not a value: "all asynchronous inputs to the MC68030 are internally synchronized in a maximum of two cycles of the processor clock". The actual delay depends on where the input edge falls relative to the clock, so it is genuinely a range and one clock is as legal as two. **Narrowed by the electrical specification**, which the user's manual defers to and which was not on disk until it was fetched: `MC68030EC/D` p. 7, parameter 35, "BR Asserted to BG Asserted (RMC Not Asserted)" is **1.5 to 3.5 clocks**, identical at 20 through 50 MHz, and parameter 37 gives the same window to BGACK-to-BG-negated. A two-clock spread between min and max is one synchroniser's worth of uncertainty — the specification agreeing this is a range rather than a figure withheld. Our three clocks sit inside it, and so would the two-clock alternative | **Not measurable against the oracle, and the previous entry here was wrong to say so**: MAME's 68000 family models no bus arbitration at all — no `BR`, `BG` or `BGACK` anywhere in `ext/mame/src/devices/cpu/m68000/` — so no second master in that emulator could ever produce a grant to time. What remains is sub-clock phase, which nothing clock-stepped represents. Closable only from hardware, or by accepting the published envelope as the answer; `arb_suite` now asserts we stay inside it |
| MC146818A periodic interrupt, six fastest rates | not modelled | `[146818]` Table 5's rates are 32768/2^n Hz. `AP_TIME_BASE_HZ` factors as 2^9·3·5^8·11, so 1.024 kHz through 32.768 kHz are not exactly representable and `ap_clock_init` refuses them. Not an approximation — the nine slower rates are exact and implemented, and the fast six are reported unsupported rather than rounded | Recompute the time base: including 32.768 kHz costs a factor of 64 and drops the representable span from 88.6 years to 505 days. Including the part's own 4.194304 MHz crystal would cost 8192x and leave 3.95 days, so the crystal can never be a clock domain in a 64-bit base at all. Cheap to do, and deliberately not done while nothing is observed using those rates |
| 68030 long bus fault frame's internal registers | Stacked as **zero**. This model has no microsequencer state to save, so the fields Table 8-6 labels INTERNAL REGISTER are written rather than skipped — a stated value, where a skipped word would leave whatever the stack already held | `[030]` Table 8-6 | An `RTE` resuming a fault *mid-instruction* cannot work from a zeroed frame |
| 68030 full-format effective address rows: which of §11.6.1's two groups an encoding selects | A **word** base displacement is free when the base is a register and costs 2 clocks when it is suppressed; a long one is never free | §11.6.1 publishes its full-format rows in two groups, one written `d16,An` and one written `B` — and its own footnote defines `B` as "0, An, PC, Xn, An + Xn, PC + Xn. Form does not affect timing", which makes the groups overlap and contradict: `(d16,An)` is 6 clocks and `(d16,B)` is 8. The reading is what makes the table consistent — **every** group A row equals its group B row with the base displacement dropped, all eight with no counterexample — and the head column agrees independently, 2 for the free rows against 4. `MC68020 User's Manual` §9.2.1 corroborates from outside: the same table with the same footnotes and *no* `d16,An` group at all, its `(d16,An)` costing 2 more than its `(B)`, so the free displacement is a 68030 addition. Strong, and still a reading | Three readings through `steptime.lua`: a full-format `(d16,An)` with the index suppressed, the same with it present, and a null base displacement as a control both readings agree on — so a disagreement there means the transcription is wrong rather than the mapping. Small, and the harness exists. Affects every full-format effective address by up to 2 clocks, never the address it computes |
| 68882 microcode version number | a stated non-zero choice, carried in every `FSAVE` state frame's format word | "The version number is an 8-bit value that identifies the microcode version of the FPCP, and **the format of this number is defined internally by the FPCP**" -- so no manual publishes a value for any part and there is nothing to transcribe. The only property the documents make observable is self-consistency: `FRESTORE` must accept what `FSAVE` wrote, and version 0 is the wild card that must be accepted whatever the part reports. Both hold for any non-zero choice, which is exactly why the choice is unconstrained rather than merely unknown | Read it from a real part, or instrument the oracle and read back the format word MAME's 68882 writes. Cheap either way, and worth doing only if some firmware is found to test the field rather than round-trip it |
| 68882 idle state frame's internal words (CU internal registers, operand register, BIU flags) | written as zeros | The same reason the 68030's stack frames give: this model has no microsequencer state to save, because its part never suspends mid-instruction. They are *written* rather than skipped so that a handler cannot read the previous program's data out from under a documented field name -- a zero is a stated value, uninitialised memory is not | Only reachable by modelling the coprocessor dialog at the CIR level, which would also be what produces a busy frame. Nothing observable depends on it until then: `FRESTORE` ignores these words on the way back in, so the round trip a program can see is already exact |
| Apollo AT map: which entry a byte address selects | `(address - base) / 2` | Neither `008778-03` nor the `019411-A00` addendum says. The region `017000`-`0177FF` is 2 KB and 128 entries of 16 bits fill 256 bytes of it, so most of the window is undescribed; the assumed indexing is the only reading with no gaps. Pinned by tests so it cannot be closed by accident | The oracle answers it, and already disagrees on the neighbouring question -- see the open plan item, which carries both halves |
| AT-compatible bus cycle times: which appendix a DS3500 keeps | `008778-03` Appendix A, the Series 3000 set — 6 MHz bus clock, so a 666 ns AT memory read, 500 ns write and 750 ns 8-bit I/O command | The manual's preface scopes it: "This document supports the Domain Series 3000 (DS3000) and Series 4000 (DS4000) systems". Our reference machine is a DS3500, and `019411-A00` — the addendum that does cover it — publishes no bus cycle times at all, nor does either engineering handbook on disk. What *is* pinned is the bracket, and it is narrow: the two published sets are 6 and 8 MHz bus clocks, every figure reduces to the same number of bus clocks in both, and the only row that differs is the memory read — four bus clocks against three. So the uncertainty is one bus clock on one cycle type, not an open range | A DS3500 hardware reference giving its BUS CLOCK, or an oracle measurement of an access to an AT device. Medium: the board-backed probe path exists, and MAME's ISA timing would need checking before its answer could be trusted. Affects how long every AT device access takes and nothing about what it returns |
| DN2500 RAM base | `0x4000000` — **corrected**, was assumed `0x1000000` | Derived from the Series 2500 boot PROM's own reset vector, exactly as this row's cost-to-close said to: `2500_BOOT_16182_8` starts with SSP `040007D0`, where `3500_BOOT_12191_7` starts with `01000180` and its RAM is at `01000000`. A reset stack pointer must land in usable memory, so the DN2500's is at `04000000` and the assumption that it matched the other 68030 models was wrong | Still `PROVISIONAL`: the reset SSP proves memory exists *there*, not where the region begins or ends. A Series 2500 allocation table would settle the extent; the oracle cannot, having no 2500 driver |

### Resolved discrepancies

Kept rather than discarded, so a future contradiction has a documented history.

- **DN4500 CPU clock: 33 MHz, not 30 MHz.** `[CFG]`'s Series 4500 Product
  Summary (p. D-108) states "32-bit MC68030 33 MHz CPU with MC68882 33 MHz",
  and its narrative section says "the 33MHz MC68030" — but its own overview
  table at p. A-11 says `MC68030@30MHZ`. 33 MHz taken as correct: two
  independent statements against one, the ordering-level summary outranks the
  marketing summary, and Motorola never binned a 30 MHz 68030 (16/20/25/33/40/50
  only). Both figures divide the time base, so nothing rests on it structurally.
  If a probe ever contradicts 33 MHz, that overview table is the reason to
  revisit.
- **DSP4500 is headless despite its heading.** `[CFG]` heads the section
  "DSP4500 Monochrome Workstation", copied from the DN4500 page. Its country kit
  (`DSPCK-*`) contains only a power cord, where the DN4500's (`DN3CK-*`) includes
  keyboard, keyboard cable and mouse. Modelled headless, like every other DSP.

## Known gaps

- **Nothing advances in time.** `ap_machine_run` steps the CPU and nothing else,
  so no device clock ticks and no status bit changes on its own. The firmware
  has not yet needed it — it stops to ask which console it has before starting
  anything timed — but every device with a counter is inert until the tick loop
  exists.
- **The display draws nothing.** Its registers, both graphics memories and the
  screen identification are modelled; the blitter and the colour lookup table
  are not. A fitted `c8p` takes 803 writes from the firmware that nothing
  answers.
- **The keyboard sends nothing.** Serial 1 channel A is wired and reachable, and
  the PROM's scan-code table at `000021D2` is read, but no scan codes are
  generated — so the machine can be typed at only through the oracle.
- **Serial framing is absent**: no baud rate, start/stop bits or parity, so a
  character crosses the DUART whole. The firmware autobauds its console by
  cycling clock select and waiting for a byte to *fail* to decode, which this
  core cannot yet make happen.
- No DMA transfers. The shared arbitration point exists (`board/ap_arbiter.c`)
  and so does an I/O adapter's route into it (`board/ap_master.c`), but nothing
  yet runs a bus cycle through either.
- **The Series 4000 Master Request Register is unmodelled and stays that way
  until a source names its bits.** `008778-03` §2.4.7's second route to bus
  mastership is "setting a particular bit in this register", and the manual
  never says which; the register at `011600` is absent from the oracle
  (`FINDINGS.md` C10). All that is known is that the boot PROM clears it at
  reset. Cost to close: a Series 4000 hardware reference naming the bit, or a
  runnable DN4500 oracle. Until then `board/ap_boardreg.c` declines the address
  rather than guessing a value, and the cascade-plus-MASTER.L route — which
  every Series 3000 and 4000 has — is the one that is modelled.
- The ring controller's register-level interface is not yet recovered; the
  manuals give its address window and block diagram but not its registers.

- **The 68882's version number is `PROVISIONAL`.** No manual publishes one for
  any part — "the format of this number is defined internally by the FPCP" — so
  the value a state frame carries is a stated choice. Only self-consistency is
  observable from the documents: `FRESTORE` accepts what `FSAVE` wrote, and
  version 0 as the wild card. Closing route: read it from a real part or the
  oracle.
- **`FMOVECR`'s constant values are not established against hardware.** They are
  computed and correctly rounded, and agree with the canonical 80-bit constants;
  what is unproven is that a given 68881 mask set holds those exact bits, since
  neither manual prints one. Closing route recorded above: instrument the oracle
  and read all 22 back.
- **Packed decimal is the only gap left in the 68882.** Every instruction the
  part has now executes: the general type entire, and `FBcc`, `FNOP`, `FDBcc`,
  `FScc` and `FTRAPcc`. What remains outside it is `FSAVE`/`FRESTORE`, which are
  the coprocessor state frames rather than instructions in the ordinary sense.
- **An `RTE` from stack frame format `$9` is declined.** The frame now *builds*,
  for the main-detected protocol violation, but resuming from it needs the
  coprocessor mid-instruction state its four INTERNAL REGISTERS words describe,
  and this model writes those as zero. A handler that diagnoses and does not
  resume works from the real fields.
- No SDL frontend. Deliberately absent rather than stubbed.
- **The oracle carries a fourth local edit.** `ext/mame`'s SC-499 now treats a
  cartridge insertion as a QIC-02 RESET, because it modelled no media change at
  all and swapping a tape under a running Domain/OS crashed it
  (`FINDINGS.md` C56). Readings taken against this build are not comparable with
  ones taken before it, which is the standing rule for every `ext/mame` edit.
- **The install is not reproducible in the oracle-reading sense.** It is a
  conversation paced by the host and the volume carries timestamps, so a rebuild
  will not be bit-identical. The image is identified by hash rather than claimed
  to be reproducible.

## Detail moved from the completion plan

The plan states each finished item as a claim plus its verification. The
reasoning that used to sit beneath those items lives here, in the order the
plan lists them. Nothing was discarded: an item whose claim already said
everything simply has no entry below.

#### Effective address times and Equation (11-2)

    through Equation (11-2). `FINDINGS.md` C9 is the reason this is now a
    named item rather than a later refinement: without it the footnoted rows
    report a component as a total, and the oracle already shows the size of
    the gap — 7 clocks against our 4 for `ADD.B D0,(A0)`.
    This is where `head` and `tail` finally earn their place. Equation
    (11-2) overlaps the effective address's tail against the operation's
    head — `CCea + [CCop - min(Hop,Tea)] + ...` — which is why both columns
    were transcribed from the start even though Equation (11-1) does not use
    them per-instruction.
    **First question, settled: the step declines.** A footnoted row is left
    at bus time alone, which `--time-instructions` shows as an *alternating*
    figure — visibly a lower bound, exactly as an instruction with no
    published figure at all reads. Reporting the component would have given
    a steady number that looks like a measurement and is short by a whole
    memory access, which every other convention in this core rules out.
    A test now asserts the decline; the one it replaced compared our total
    against `CC` and `NCC` and *passed*, because both sides were the same
    component — the blind spot C9 records, made concrete.
    **Second question, now recorded:** `max(microcode, bus)` cannot be
    extended to these rows. `docs/references/M68030_TIMING.md` works it
    through — the warm case needs a smaller answer against a smaller bus and
    the cold a larger one against a larger, which `max` cannot give from one
    microcode figure because it is monotonic in both. The refinement is one
    question per bus cycle, *is the microcode waiting on this?* — a prefetch
    is not, an operand read feeding the current operation is — giving
    `max(microcode, hideable) + blocking`. That is a two-bucket change
    rather than a rewrite, but it is not to be made before the effective
    address tables exist, so that both sides of the composition are
    published numbers rather than one published and one inferred.
- [x] **A methodological correction the tables forced.** §11.6's assumption
      list includes "The data cache is not enabled", and the sampling
      helpers had it on. Comparing a figure measured with it on against one
      computed with it off is not like-for-like, and the difference is an
      operand read per repeat — invisible for the register forms, which is
      why a dozen rows passed the two-sided check before this surfaced. A
      published table's *assumptions* need transcribing as carefully as its
      numbers.
- [x] **The tables themselves**
      (`src/core/cpu/m68030/ap_m68030_ea_timing.c`): §11.6.1's fetch and
      §11.6.3's calculate rows for the modes without a full-format
      extension word.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `ea_timing_suite`, 8 tests — a register operand costing
      nothing either way and its head marked inapplicable; fetching never
      costing less than calculating, for every mode, which a swapped
      transcription would break everywhere at once; the two tables differing
      by exactly the operand read; the relative heads distinguished from the
      plain ones; an immediate costing by the words it occupies, with byte
      and word alike per Table 2-3; the immediate absent from the calculate
      table; and the long absolute being the one fetch row whose two columns
      differ.*
- [x] **Composing them.** Half of the verification this item names is now
      met: **the second worked example of §11.3.4 comes to 40 clocks**,
      which is Motorola's arithmetic on Motorola's figures and the only
      published number that exercises Equation (11-2) rather than (11-1).
      Detail in `PROJECT_STATUS.md` and
      `docs/references/M68030_TIMING.md`.
      *Verification: `ea_timing_suite`, 4 further tests (12 total) — the
      40-clock example with the components fed in **as the example prints
      them** rather than from our tables, since feeding the transcription in
      would move both sides of the comparison together; a register operand
      contributing no component at all, checked on a case where a zero-cost
      one would over-count; the "2+op head" notation resolving against its
      operation; and four of our transcribed rows agreeing with the same
      example, which is a check against a different page from the one they
      were read off. `overlap_suite`, 3 further tests (15 total) — the
      no-cache case composing by addition against §11.3.3's own "2 + 7 = 9"
      and "9 + 7 = 16", and the two columns shown not to be the same
      function.*
- [x] **Equation (11-2) is Equation (11-1) over *components*.** It reads as
      a second rule and is not one: every term is a component's cache case
      less the lesser of its own head and the previous component's tail, and
      (11-2) only adds that an instruction contributes *two* components. One
      accumulator therefore serves both, which is why
      `ap_m68030_overlap_add_component` now sits beneath
      `ap_m68030_overlap_add`.
- [x] **The no-cache case composes by plain addition**, per §11.3.3, and
      `ap_m68030_no_cache_total` is that second rule. Kept a separate
      function deliberately: running `NCC` figures through head and tail
      would subtract an overlap the published number already excludes.
- [x] **A per-instruction comparison against a published `NCC` is the wrong
      comparison, permanently.** §11.3.3 works an instruction that costs
      "eight clocks for even alignment and 10 clocks for odd alignment, an
      average of nine" while the *pair* it belongs to costs "16 clocks for
      both even and odd alignment". So the alignment difference moves
      between adjacent instructions rather than adding to the stream, 9 is a
      figure the hardware never exhibits, and the right unit of comparison
      is a sequence. This core already exhibits that alternation
      (`FINDINGS.md` C7).
- [x] **Composed into the step, and `FINDINGS.md` C9 closes.**
      `total = microcode + measured operand bus + prefetch cost`, where the
      microcode is `CC − 2(r + w)` from the published `(r/p/w)` and the bus
      half is what this core measures -- so a wait state or a cache hit
      still moves the answer, which is the difference between this and a
      cycle-table model.
      Detail in `PROJECT_STATUS.md` and
      `docs/references/M68030_TIMING.md`.
      *Verification: `ADD.B D0,(A0)` at **6 warm and 7 cold averaged over
      both alignments** -- the manual's composed figure and the oracle's
      measurement -- with both 6 and 8 asserted to occur, so the average is
      not four equal numbers. `machine_suite`'s decline test became this
      one.*
- [x] Three things the wiring forced, each found by a number moving that
      should not have: the `*` and `**` footnotes name **different tables**
      and can no longer share a flag; the exposure rule was being applied to
      rows it was derived to exclude, so applicability is now data on the
      row rather than a list in a test; and a **pipe refill is not the row's
      own prefetch**, so it is charged where it happens rather than replaced
      by a published figure derived for something else.
      *Verification: `timing_table_suite`, 16 tests, including the
      classification checked against each instruction's length and whether
      it changes flow rather than against the figures it is used with.*
- [x] **§11.6.1's full-format rows, transcribed and selectable by the
      extension word** -- sixteen entries covering every combination of base
      and outer displacement, which is the whole space a full-format word
      can encode. The reading that decides between the table's two
      contradictory groups is `PROVISIONAL` with its measurement named.
      Detail in `PROJECT_STATUS.md` and
      `docs/references/M68030_TIMING.md`.
      *Verification: `ea_timing_suite`, 7 further tests (19 total) -- every
      combination resolving to a consistent row; the reading isolated in a
      test of its own rather than buried in a sweep, with a long base
      displacement as the control that a transcription making *every*
      displacement free would fail; an indirection costing a second read at
      every base displacement; the three indirection kinds sharing their
      figures, which the table's own note about Xn says from the other side;
      and the worked example's `fea ([B])` now reachable by lookup, so that
      test's hand-supplied inputs and this table agree from two different
      pages.*
- [x] **§11.6.3's full-format rows too, and they confirm the reading on a
      second table.** Every group A row equals its group B row with the base
      displacement dropped in the calculate table exactly as in the fetch
      one, so the pattern holds over sixteen rows across two independently
      typeset tables with no counterexample. The head column corroborates it
      in a way the fetch table cannot: there the group A rows carry a plain
      head where `(B)` carries "6+op head", so the groups differ in *kind*
      and not only in value.
      *Verification: `ea_timing_suite`, 3 further tests (22 total) -- the
      two tables' patterns asserted together rather than separately;
      calculating reading one fewer than fetching at every row, which is
      §11.6.3's "fetch time ... only for the first level of indirection";
      and the head kinds told apart.*
- [x] Four more `p` counts corrected in the calculate table, the same defect
      the fetch table had and found the same way -- by reading the page
      rather than a text extraction of it.
- [x] **§11.6.2, Fetch Immediate Effective Address, transcribed and wired**
      -- so the `**` rows are priced rather than declined. It is keyed by the
      immediate's size *and* the destination mode together, because one
      entry covers both halves, which is exactly why such a row could never
      have been priced off §11.6.1.
      *Verification: `ea_timing_suite`, 4 further tests (26 total) --
      including that the two size columns are **not** a scaling of each
      other, `(An)` differing by one clock and `(An)+` by two, so a model
      scaling one column by operand size would be wrong in both directions;
      a long immediate never costing less than a word one, which a swapped
      column would break everywhere at once; the absent `An` destination row
      reported absent, the table agreeing with the opcode map rather than
      having a gap; and the `%` relative heads distinguished from the plain
      ones.*
- [x] **Every row is now priced; none declines.** The change-of-flow rows
      fell to §11.3.3's page: the target's alignment decides *where* the
      refill reads, not how many bus cycles it takes -- a three-deep pipe
      wants two either way -- so nothing is averaged and the published
      difference is the exposure. The three-word rows fell to the same
      arithmetic as a single word, the two alignments differing by one
      fetch, so the larger case is twice the published average and the
      smaller is free.
      Detail in `docs/references/M68030_TIMING.md`.
      *Verification: `timing_table_suite` asserts **zero** rows classified
      unknown, so a row added without a class decision fails rather than
      being priced by whichever rule sits first.*
- [x] Two readings **landed as `PROVISIONAL`** rather than left open, which
      is what `CLAUDE.md` prescribes for a documented approximation with a
      reason and a cost to close: the reading that selects between §11.6.1's
      and §11.6.3's two row groups, supported by sixteen rows across two
      tables with no counterexample and corroborated by the 68020 manual;
      and the one-clock bound §11.3.3's "rounded up" leaves on a published
      difference of 1, which the pair provably cannot separate. Both are in
      `PROJECT_STATUS.md`'s PROVISIONAL table with the measurement that
      would close them.

#### The 68030 table walk

    `ap_m68030_desc`'s rules, and fill the ATC. This is the piece that joins
    the four MMU modules together, and the first one whose *timing* is
    interesting, since it is where the bus cycles are. The manual states ATC
    translation "is always completely overlapped by other operations; thus,
    no performance penalty is associated with ATC searches" — so an ATC hit
    must cost nothing in our timing, and a *miss* is where the table walk's
    bus cycles appear.
- [x] **The table search** (`src/core/cpu/m68030/ap_m68030_walk.c`),
      `[030]` §9.2 and §9.5. Splits the logical address with
      `ap_m68030_tc`, walks the tree, applies `ap_m68030_desc`'s rules to
      each descriptor, and reports `descriptor_fetches` — the quantity a
      timing probe measures, and the reason a three-level tree costs more
      than an early-terminating one.
      *Verification: `walk_suite`, 13 tests — one fetch per level, an
      invalid descriptor stopping the search where it is found, early
      termination both shortening the search and taking the unconsumed
      index bits as offset, an indirect descriptor costing its extra fetch
      and being rejected unless it points at a page descriptor, WP/S/CI
      accumulating down the tree, a limit violation aborting *before* the
      fetch it would have indexed (0 fetches, not 1), a bus error ending
      the search, and the 8-byte stride of a long-format table.*
- [x] **Descriptor writeback: the U and M bits.** "During a table search,
      the U bit in each descriptor that is encountered is checked and set if
      it is not already set", and M is set for a write access under
      `ap_m68030_desc`'s rule. Each update is a bus *write*, so this is
      timing rather than bookkeeping: `[030]` §11 p. 11-56 counts a table
      search in reads and writes separately and states that "an RMC cycle to
      set the U bit is counted as one read and one write". The update is
      expressed as "set U / set M at this address" rather than as a long
      word, for the same reason the fetch returns a decoded descriptor —
      the status bit positions are deferred, not guessed.
      *Verification: `walk_suite`, 13 further tests — one write per
      descriptor with U clear and none when it is already set, a write
      access costing exactly one more than a read of the same tree, an
      already-modified page costing nothing, WP and a supervisor violation
      each suppressing M, the read half of a read-modify-write still
      setting it, an invalid descriptor getting no write at all, the M bit
      landing on an indirect descriptor's target rather than on the
      indirect itself, and a bus error on the write half setting B.*
- [x] Corroborated against the `MC68851 PMMU User's Manual 3ed` §5.1.5.3.11,
      whose update table is explicit where `[030]` is prose — U clear with M
      unchanged is an RMW, U and M together are a *single* operation, U
      already set with M clear on a write is one write, and both already set
      is no write at all. Our one-write-per-descriptor cost model matches it
      row for row. Worth recording because it is independent of the 68030
      manual and pins the case that is easiest to get wrong: setting two
      bits in one descriptor must not cost two cycles.
- [x] **One reading, `PROVISIONAL`, and every source is now exhausted: when a
      supervisor violation suppresses the U update.** `[030]` says the U bit
      is set "except *after* a supervisor violation is detected" without
      saying whether a descriptor whose *own* S bit causes the violation
      still gets its own U set. We fold that descriptor's S in first, so it
      does not.
      - The **68851 manual** repeats the sentence about a pointer being
        fetched for an address denied at *another* level and drops the
        supervisor clause entirely, so it does not arbitrate.
      - The **oracle has no opinion**: `FINDINGS.md` C2 found
        `update_descriptor()` never consults supervisor state at all, so it
        cannot distinguish the two readings -- it implements neither.
      - The **web** has nothing: the question does not appear in the
        circulating 68030 MMU write-ups, which cover descriptor formats and
        `PTEST` and stop short of the history-bit gating.
      - So this needs **real hardware or a Motorola erratum**. Landed as
        `PROVISIONAL` rather than left open: the reading is made, documented
        and consistent with the manual's other sentence, and what is
        outstanding is confirmation rather than a decision. It is in
        `PROJECT_STATUS.md`'s table with its cost to close.
- [x] **Fill the ATC from a completed search**
      (`ap_m68030_walk_fill_atc`), so a miss populates the entry a hit then
      serves for free. This is the join between `ap_m68030_walk` and
      `ap_m68030_atc`, and the point at which the "hit costs nothing, miss
      costs the search" claim becomes measurable end to end.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `walk_suite`, 7 further tests — a filled entry turning
      the next access into a hit at the right physical address, WP and CI
      reaching the entry, each of the three fault kinds cached as a faulting
      entry (including a supervisor violation, where the search itself
      succeeded and only the access was illegal), and the end-to-end form of
      the §9.4 timing rule: a read fills M clear, so a later write is a hit
      that still forces a search, and refilling after that search makes the
      write free.*
- [x] **Descriptor status bit positions: derived, and labelled as derived.**
      Detail in `PROJECT_STATUS.md`.
      *Verification: `walk_suite`, 7 further tests — short table and page
      descriptors decoding their different address widths and status sets, a
      table descriptor not decoding CI or M (page-descriptor fields whose bit
      positions fall inside its ADDRESS), an indirect descriptor having no
      status bits at all and so costing no history write, long-format LIMIT
      as fifteen bits under the L/U flag, and a three-level tree built from
      real memory words walking to the same physical address as the
      hand-built one.*
- [x] **Confirmed against the oracle** (`FINDINGS.md` campaign C1). Every
      derived position matches `m68kmmu.h`'s field constants field for
      field — DT, WP, U, M, CI, S, and both address masks — which is a sixth
      source independent of the five that produced the derivation. Two
      behaviours derived from prose are confirmed as code besides: U and M
      are written in a *single* store, and no history bit is written to an
      invalid descriptor.
- [x] **Divergence found and classified `oracle-wrong`** (C2): `[030]`
      §9.5.1.1 gates the U update on "except after a supervisor violation is
      detected" and the M update on "or a supervisor violation", and the
      oracle implements neither — `update_descriptor()` never consults
      supervisor state. We keep ours, since the manual states the condition
      twice for two separate bits. It changes history bits only on a search
      that already faults, but that is exactly a supervisor-only tree probed
      from user state.
- Cross-reference: the supervisor-violation U-bit reading above is the
  same question, recorded once there rather than twice.
- [x] 68030 on-chip instruction and data caches, and their effect on bus timing.
  *Verification: hit-vs-miss measured through the bus state machine — a hit
  costs 0 clocks, a burst line fill 5, a non-bursting miss 2. A probe
  against the oracle remains worthwhile and is listed under the probe suite,
  but the claim no longer rests on one.*

#### A data register write is partial and an address register write never is

Two rules that look alike and are opposites. A data register write of a byte or
word "only uses or changes the appropriate lower 8 or 16 bits" and leaves the
rest alone; an address register write is always full width — "the source operand
is sign-extended to a long operand and the operation is performed on the address
register using all 32 bits".

So `MOVE.W #$FFFF,D0` leaves `D0`'s upper half untouched, and
`MOVEA.W #$FFFF,A0` sets `A0` to `$FFFFFFFF`.

Each failure mode is silent and they point opposite ways. Applying the data rule
to an address register leaves a stale upper half that later long operations use
without complaint; applying the address rule to a data register destroys live
data in the half that should not have moved. `operand_suite` applies both rules
to the *same* operand value side by side, which is the comparison that catches
one being used where the other belongs.

#### Family `0100` needs three decoders, and the order they are tried in

The `$4E` control group goes first because its top byte is fixed and so cannot
be mistaken for anything else; then `LEA`/`CHK` and the `$48`/`$4C` forms, which
all carry bit 8 set; then the single-operand group, which requires it clear. The
last two cannot collide for that reason, and the first cannot collide with
either.

That ordering is a property of how the encodings nest rather than a preference,
which is why it is stated in the decoder's header: a reader who reorders them to
taste gets a decoder that still passes most tests.

#### The extended forms share one `Z`, and that is the point of them

`ADDX`, `SUBX`, `ABCD`, `SBCD` and `CMPM` carry the documented "cleared if
nonzero; unchanged otherwise" `Z` rather than the ordinary "set if zero". That
is what lets a single `Z` describe a whole multi-precision value instead of just
its last word: each step can only ever *clear* the flag, so a `Z` surviving to
the end means every word was zero.

An implementation that set `Z` the usual way passes every single-word test and
gets multi-precision comparison wrong in exactly the case the instructions exist
for — a long value whose final word happens to be zero compares equal.

#### The machine knows which model it is, and keeps that model's time

`ap_machine_init_model` takes a model id and `ap_machine_init` is that with the
DN3500 — the reference superset — so every caller that existed kept the machine
it had. What makes that more than bookkeeping is that the row is *read*.

**Two fields are consulted, and each was a table pretending to be a model until
it was.** The first is `has_module_calls`, which the step now carries from
`ap_cpu_features()`: a DN3000 executes `CALLM` where a DN3500 raises the illegal
instruction exception, decided by the one table rather than by a conditional in
the step. The second is `cpu_hz`, and it had sat in every model row since Phase
0 with nothing in the core reading it.

**A machine with no rate keeps no time.** `ap_clock_duration` multiplies by a
period, and a period of zero is zero however many cycles it multiplies, so every
machine a probe built ran at 12, 20, 25 or 33 MHz on paper and at no rate at all
in fact — a DN3000 and a DN4500 returned the same elapsed time for the same
program, which is exactly the number this core exists to get right. The rate now
arrives with the model, and every probe hash in `tests/goldens/probes.txt` moved
because of it while no other column did: the probes had been running with a
zero-rate clock, and their timing is now counted.

**There is deliberately no setter.** The frontend used to look `dn3500` up by
name and hand the rate to a machine that already knew which model it was — a
second place where machine variance lived, which `CLAUDE.md` puts in
`src/core/model/` and nowhere else. The rate setter is gone rather than merely
unused: with it in the header a caller can build a DN3500 whose processor runs at
some other machine's speed, and the fact that nobody does today is not a property
that holds itself.

The refusal it carried is not lost, only moved to where it cannot be bypassed.
`ap_clock_init` still refuses a frequency `AP_TIME_BASE_HZ` does not divide
exactly rather than rounding it, and `time_suite` asserts the base divides every
model's clock — so a model added with an unrepresentable rate reddens a test
instead of quietly producing a machine that keeps no time. That is a stronger
guarantee than the one it replaces, which only ever fired if a caller checked
the return value.

The verification is a ratio rather than a figure: a DN2500 and a DN3500 are both
68030s, so the same program costs the identical number of cycles on each and the
only thing that can differ is the rate. Their elapsed times stand at exactly
25:20, cross-multiplied rather than divided because a ratio checked by division
passes on two zeroes — which is the bug this item fixed.

#### A probe can run on a board, not only on flat RAM

Every probe until now ran on "a 68030 on flat RAM and nothing else", which is
what made side-loading cheap and is why the probe harness came before the boot
PROM route. It also meant no probe could touch a *device*: the register is
unmapped on flat RAM, so our side faults where the oracle's `dn3500` answers.
That is what left the device verification lines — interrupt ordering, DMA
transfers, timer self-timing, the SIO byte stream — with no route at all.

`--probe-file` now takes `board 1`. It builds a whole core board and no boot
PROM: `ap_board_init` does not need one, and a probe is side-loaded precisely so
that no firmware runs. The probe is written through the *board* rather than
`ap_machine_write`, which is the operator's view of flat RAM and knows nothing
of where a model puts its memory.

The consequence worth having is that a board probe loads at the model's RAM base
— `01000000` on a DN3500 — which is where the oracle's loads. Both sides then
run the *same addresses*, and the diff stops needing the base offset that every
existing probe carries.

Measured rather than asserted: the same probe reading a DMA register runs in
three instructions with no bus error on a board, and on flat RAM takes 25 bus
errors and never terminates.

**And it immediately surfaced a divergence class.** Reading the 8237A's all-mask
register is marked "Illegal" by `[8237]` Figure 6. This core returns zero — "the
part drives nothing, and a caller reading here has a bug this core should not
paper over" — where MAME returns `0F`, which is what `FINDINGS.md` C13 used as a
placement fingerprint. Neither is wrong: the datasheet defines no value. It is
registered here so that the first board-backed oracle diff does not read it as a
defect.

#### The first published wait states, and two tables that check each other

A device could lengthen its own cycle and nothing declared a figure, so every
region answered at the minimum and contention was emergent in *who* held the bus
but never in *how long*. `008778-03` Appendix A (Table A-1, Series 3000) and
Appendix B (Table B-1, Series 4000) are the figures, and they are large: an AT
card answers in hundreds of nanoseconds where the minimum cycle at 25 MHz is 80.

**The two tables say the same thing at two clock rates, and that is checkable.**
Appendix A is a 12 MHz machine and B a 16 MHz one, and the figures look
unrelated until each is divided by its own table's BUS CLOCK period — `#26`, 166
and 125 ns. Then almost every row is the same number of bus clocks in both:

| Row | Series 3000 | Series 4000 |
| --- | --- | --- |
| `#30` memory write cycle | 500/166 = 3.0 | 375/125 = 3.0 |
| `#37` 16-bit I/O command | 250/166 = 1.5 | 185/125 = 1.48 |
| `#48` 8-bit I/O command | 750/166 = 4.5 | 560/125 = 4.48 |
| `#17` MEMR.L width | 330/166 = 1.99 | 250/125 = 2.0 |
| `#55` BALE width | 830/166 = 5.0 | 625/125 = 5.0 |
| `#80` 0WS memory cycle | 415/166 = 2.5 | 313/125 = 2.5 |

The residue is the tables printing whole nanoseconds for a 166.67 ns period.
`atbus_suite` asserts the agreement to within 0.05 of a bus clock rather than
describing it, so a digit wrong in either transcribed column fails a test — the
transcription checks itself against a second, independently typeset copy.

What it says is what an AT bus cycle *is*: a fixed number of bus clocks, the two
appendices differing only because their bus clocks do. The one row that genuinely
differs is the memory read cycle, `#18` — four bus clocks on the Series 3000 and
three on the Series 4000, a real wait state on the slower board and not an
artefact of the division. It is also the only row where the direction matters,
which is why the callback takes one.

**Which row is which cycle came from the diagrams, not the table.** Both tables
carry *two* rows called "IOR.L, IOW.L Width Asserted", `#37` and `#48`, with no
note distinguishing them. Figure B-3 "Bus 16-Bit I/O Read Cycle" annotates
IOR.L's width with `37` and Figure B-7 "Bus 8-Bit I/O Read Cycle" with `48`.
Those numbers are inside the drawings; no text extraction carries them, and
reading the page images was the whole of settling it. The same figures label the
two clocks "Internal signal on the CPU/Motherboard. Not available on the Bus",
which confirms BUS CLOCK is CLOCK halved on both boards.

**A published lower bound for I/O, used anyway.** Neither table gives an I/O
*cycle* time — it gives the command width, and a cycle is that plus the address
setup before it (`#44`) and the hold after (`#39`), published separately. The
figure modelled is therefore the command width, deliberately not summed into a
total: the same rule this project applied to the 68030's footnoted timing rows,
where reporting the component as the lower bound it is beat constructing a total
that would read as measured. The alternative is not "no approximation" but no
wait states at all — 80 ns against a documented 750. Cost to close: a published
AT I/O cycle time, or a measurement.

**The board answers a duration; the machine converts it.** `ap_board_access_time`
returns `AP_TIME_BASE_HZ` units, and `machine_wait_states` divides by the CPU's
period and subtracts the two clocks §11.6 assumes anyway. That division of labour
is the same one the machine's clock has — how long a port takes is the port's
property, how many clocks that costs is the asking processor's — and it means the
wait-state count is written down nowhere: a DN3500 pays 19 clocks and a DN3000
pays 9 for the identical card, both falling out of one published figure and two
rates. That only became expressible when the machine started reading `cpu_hz`.

**Decided by address, not by device.** Table 2-8 puts the AT bus in two address
windows, so the figure follows the address: the disk, the tape, the floppy and an
empty slot are all inside the I/O window and get the I/O cycle without anyone
deciding, device by device, which is an AT card. Eight bits wide, because that is
what a card gets when it does not assert `MEM_CS16.L` or `IO_CS16.L` — the AT's
default rather than a choice.

The one consequence to watch is the display: both graphics memories decode inside
the AT *memory* window, so a frame buffer access is charged an AT memory cycle. If
the controller sits on a local bus instead, that is too slow by a large factor. It
is recorded rather than guessed either way, and the region counters keep it
visible.

**`PROVISIONAL`: the DS3500 is in neither appendix.** `008778-03`'s preface says
"This document supports the Domain Series 3000 (DS3000) and Series 4000 (DS4000)
systems"; our reference machine is a DS3500, and `019411-A00`, the addendum that
does cover it, publishes no bus cycle times at all. Neither sibling handbook on
disk has wait states either. What *is* pinned is the bracket — an AT-compatible
bus runs at one of these two rates, the cycle counts agree at both, and only the
memory read differs. The board takes the Series 3000 set, and the disagreement the
other choice could produce is one bus clock on a memory read. Cost to close: a
DS3500 hardware reference giving its BUS CLOCK, or an oracle measurement of an AT
device access.

#### An I/O adapter's route to the arbiter, and the encoder that is really two

`board/ap_arbiter.c` answers *who gets the bus*. `008778-03` §2.4.7 answers how
an AT card becomes one of the askers, and the answer is not "assert a request
line" — the card requests through a **DMA channel**, and the bus it ends up with
is only the card's because that channel was programmed in cascade mode.

The paragraph is quoted in full in `board/ap_master.h`, and every rule in the
module is one of its clauses:

- **Cascade mode is the load-bearing part.** The same DRQ on a channel in any
  other mode wins the same arbitration and gives the card a *transfer*, not the
  bus: the controller drives, which is the DMA controllers' item rather than
  this one. So ownership is gated on the mode, and the module reports the other
  case as "the controllers may drive".
- **DACK follows mastership, it does not precede it.** The board can only
  acknowledge a channel it has won the bus for, so the acknowledgement is the
  arbiter's answer read back rather than anything this module decides.
- **Ownership ends when *both* signals are released** — "until it releases the
  DRQx and MASTER.L signals". That is stronger than it looks: an adapter that
  drops DRQ while still holding MASTER.L keeps the bus, so the module holds the
  arbiter's request line asserted on its behalf once acknowledged. Passing the
  bare DRQ line through would hand the bus back mid-transaction with the card
  still driving it.
- **MASTER.L inhibits AEN**, which is what lets the new master address I/O cards
  at all: AEN asserted tells a card the address belongs to a DMA controller.
- **The route runs through the part, not past it.** The request is decided by
  `ap_i8237_service_pending`, which honours the mask and the controller-disable
  bit, so software that has masked the channel has closed the route. That falls
  out of asking the part rather than the pin.

**Which DRQ line and which channel is not decided here.** The caller supplies
both, because `board/ap_dma.h` refuses to claim this board's cascade wiring —
the equivalent assumption about the interrupt controllers was wrong on this
machine (`FINDINGS.md` C11), and the DMA cascade is to be measured once
transfers exist. A module that hard-wired a channel would have made that
measurement look unnecessary while being wrong the same way.

**The finding worth keeping: there are two priority encoders in series, and the
first one is not the arbiter's.** A test written to show two adapters ordered by
the AT bus's "DRQO having the highest priority" order failed, and the model was
right. A controller has one request output, so its own encoder resolves its four
channels before anything downstream is asked — an adapter on a low-numbered
arbiter line loses to one on a low-numbered *channel*. The AT's DRQ0-through-7
order is what the cascaded pair of controllers implements, not a separate
encoder sitting above them. Both halves are now pinned: two adapters on one
controller are ordered by channel, two on different controllers by line.

**The Series 4000 route is a recorded gap, not an omission.** §2.4.7's second
paragraph gives an alternative — "In the Series 4000, an alternate method of bus
arbitration exists that implements a Master Request Register. By setting a
particular bit in this register, an external processor asserts its DMA Request
signal to the system processor." Which bit is never stated; the register at
`011600` is absent from the oracle (`FINDINGS.md` C10) and `board/ap_boardreg.h`
declines it for that reason. Modelling it would mean choosing a bit number no
source supplies. What is known and not enough: the boot PROM executes
`CLR.B $00011600` on every pass through its reset path, which is what a
bus-mastering request register would want at reset and confirms nothing about
the arbitration path — not the read-back value, and not the effect of a set.
Cost to close: a Series 4000 hardware reference naming the bit, or a runnable
DN4500 oracle that has the register. The same paragraph also gives the one
figure that *is* stated — "The address translation map in the Series 4000
restricts external Masters to transfers of 512 KB to or from main memory" —
which `board/ap_atmap.h` already carries.

#### A device can lengthen its own bus cycle

`STERM` used to be answered on the first sampling opportunity whatever replied,
so every device was equally fast. Contention could then be emergent in *who*
held the bus and never in *how long*, and no measured figure could come from a
device's own speed.

`[030]` §7.3.1: "If DSACKx is not recognized by the start of S3, the processor
inserts wait states instead of proceeding to S4 and S5 ... the processor
continues to sample the DSACKx signals on the falling edges of the clock until
one is recognized." With none, "the bus cycle runs at its maximum speed (three
clocks per cycle)". The bus state machine already modelled wait states and a
device that had not answered; nothing could say so.

The memory system now declares how long it takes, through one callback on the
access context — **not** a field on the fill answer. A write has no fill answer,
and both directions must charge the same device the same time or a program could
be made faster by writing. It is also the truer shape: how long a port takes to
answer is a property of the port, which is what drives `DSACK`/`STERM` in §7.3.

Termination is **withheld** from the bus rather than added to a total
afterwards, so the wait states are counted by the state machine and a cycle
lengthened here lengthens the instruction, the probe and the golden without any
of those layers being told. A `BERR` is never withheld: a device that cannot
answer is not a slow one, and delaying it would delay the exception rather than
the data.

`NULL` means the minimum — what this core did before, and what §11 assumes
throughout ("All memory accesses occur with two-clock bus cycles and no wait
states") — so no published figure moved and every golden is unchanged.

A burst is the case worth checking: its four beats are one cycle held open, so
the wait states are paid **once** and not per long word. Charging per beat would
look right on a single read and quadruple a line fill.

#### Returning from an exception, and three traps in the neighbouring instructions

**`RTE` is a loop, not a special case.** The throwaway frame is what makes it
one: "the processor reads the status register value from the frame, increments
the active stack pointer by eight, updates the status register ... and then
begins RTE processing again" — on whichever stack the restored `S` and `M` bits
now select, and the frame it then finds "may be any format (even another
throwaway frame)". An undefined format is a format error, vector 14.

**`RTR` restores only the condition codes.** "The supervisor portion of the
status register is unaffected", because `RTR` is unprivileged and restoring the
system byte would make it an instruction any user program could use to enter
supervisor state.

**`BSR`'s condition field is `F`** — the encoding that means *never* for a
`Bcc`. Testing the condition without excluding it pushes a return address and
then falls through, so every subroutine call leaks a stack word and returns to
the wrong place. Found while wiring `BSR` to the stack.

**`MOVE An,USP` writes the USP directly** rather than through `A7`. It only
executes in supervisor state, where `A7` names the `ISP` or `MSP`, so going
through `A7` would move the wrong stack and leave the one being set up
untouched.

#### Taking an exception on the 68030

    the VBR, and loading the PC (`ap_m68030_take_exception` in
    `ap_m68030_step.c`). Needed an instruction unit and a memory system, so
    it landed with them rather than here — the same split as the caches,
    where structure and cost were separable.
    §8.1's four steps, and their **order** is the whole of the difficulty:
    the status register is copied *before* S is set, and it is the copy that
    is stacked. Stacking the modified one survives casual testing — the
    handler runs, RTE returns — but returns to a user program with S still
    set. Nothing faults; the privilege boundary is simply gone. The frame
    goes on the *active* supervisor stack, read after S is set, so a
    user-state exception builds on the ISP and leaves the USP alone.
    Which frame comes from Table 8-6, transcribed into
    `ap_m68030_frame_for_vector`: the six-word frame's extra long word is
    "the address of the instruction that caused the exception", distinct
    from the stacked PC, which points at the *next* one. A six-word
    exception given a four-word frame leaves RTE reading a vector offset out
    of an address.
    Wired in so far: **divide by zero** (the tail of the instruction
    semantics item above, now closed) and **TRAP #N**, whose four-bit field
    is an index into Table 8-1's trap range and not a vector number.
    **Interrupts land here too**, and they differ from every other exception
    in three ways. The status register copy is taken **before** the priority
    mask is raised — stacking the raised mask instead leaves the interrupted
    code running at the handler's priority for ever after, never receiving
    another interrupt at its own level, and nothing faults. The vector comes
    off the bus through an acknowledge cycle, with `AVEC` selecting an
    autovector and a bus error during the cycle meaning *spurious* rather
    than a fault — which is what keeps a machine with a misbehaving device
    running. And "If the M bit of the status register is set, the processor
    clears the M bit and creates a throwaway exception stack frame on top of
    the interrupt stack": one interrupt, **two frames on two different
    stacks**, and clearing M first is what moves A7 between them, so the
    order is not an implementation detail.
    Level 7 is recognised on the *transition* to 7, not on the level, so
    holding the line there does not re-interrupt — a model that read the
    level alone would never let the handler make progress. An interrupt also
    ends a `STOP`, which is what `STOP` was waiting for.
    **Reset now performs §8.1.1's ten steps** and is no longer declined.
    Four of them are the ones a plausible implementation drops, none of
    which faults when missed: "setting the supervisor bit **and clearing the
    master bit**"; the vector base register zeroed; the caches' freeze and
    **write-allocate** bits cleared along with their enables; and
    translation disabled in the TC *and* in both transparent registers.
    Two explicit negatives are as load-bearing as the steps -- reset "does
    **not** flush the address translation cache (ATC), nor does it save the
    value of either the program counter or the status register" -- so there
    is no frame, and an ATC entry must survive. A model that flushed it
    would be tidier and wrong.
    *Verification: `step_suite`, 2 further tests (193 total) -- the ten
    steps checked as a whole from a processor deliberately left in the
    state each one has to undo, with an ATC entry asserted to survive; and
    nothing stacked, checked by counting stores rather than by inspecting
    the stack pointer alone.*
    **An encoding the effective-address category tables forbid now takes the
machine's trap.** `MOVE`'s destination must be data alterable, `LEA`'s source
must be control, the MMU's operand must be control alterable — rules transcribed
from the manual's own tables. A word failing one is not a valid MC68030
instruction, so §8.1.5's answer is the illegal instruction exception. This core
reported `UNIMPLEMENTED` instead, which says "this model has not finished" — the
opposite of the truth, and it stopped a machine the hardware would have carried
on through a handler.

The refusal is carried as a **vector** rather than a flag, because the answer is
not always the same one: an F-line word with cpID 0 — the MMU's own encodings —
is p. 8-10's "unimplemented instruction with an F-line opcode" and takes **vector
11**, not 4. A boolean would have sent every `PMOVE` refusal to the
illegal-instruction handler, which is wrong in the way that looks right.

Found by sweeping all 65536 opcodes through the step and counting the outcomes,
which is the coverage-of-the-specification check rather than coverage of what
runs. **791 words were reclassified.** Four `step_suite` tests had been asserting
the old verdict and now assert the trap and its vector.

The narrowness is deliberate and is C89's rule applied again: only a refusal
coming from a transcribed category table becomes a trap. Every other `return
false` still reports our gap, because that is what it is — and the sweep leaves
**1830 words still `UNIMPLEMENTED`**, which is now a measured number rather than
an impression. The largest remaining group is named as an open plan item: the
single-operand, immediate and shift groups enforce no categories at all, so
`NEGX.B #imm` is refused only because writing to an immediate happens to fail.
`MOVEP` is in that count too, and is a genuinely missing instruction rather than
a misclassification.

**The category tables are now enforced in the single-operand, immediate and
shift groups too, and each rule came from its own instruction page.** The
`[PRM]` states the category in prose on every page — "Only data alterable
addressing modes can be used" — and that sentence, not a generalisation from a
neighbour, is what each check transcribes.

The numbers matter more than the mechanism. C95 moved 791 words from *our gap*
to *the machine's refusal*; this moves **578 words that were executing** into
refusing. Those are the expensive ones: the category header has always said that
accepting words the processor refuses is "the wrong direction to be wrong in,
because a real program never contains them and only a broken one benefits", and
that is exactly what was happening.

Three rules no single category expresses, and each would have been got wrong by
applying a neighbour's:

- **`TST` reaches every addressing mode**, immediate and PC-relative included —
  a 68020 widening, its PC rows footnoted "do not apply to MC68000, MC680008, or
  MC68010". Its neighbours are data alterable, so the group's rule would have
  refused three forms this processor runs. Its one restriction is a *size* rule:
  "Address register direct allowed only for word and long", so `TST.B An` is the
  single illegal `TST` — and this core executed it.
- **`CMPI` is data, not data alterable**, because it only reads. Its table also
  dashes the immediate, but the decoder already owns that row: `mode 111
  register 100` carries the `CCR`/`SR` forms, and `CMPI` has none. Checking it
  again in the executor would have been a second copy that never runs.
- **`BTST`'s two forms disagree about the immediate.** `BTST Dn,<ea>` lists
  `#<data>`; the static `BTST #n,<ea>` on the facing page dashes it, having
  spent the immediate on its bit number. Unlike `CMPI`'s, this one *cannot* live
  in the decoder, because the bit-operation rows decode their effective address
  directly instead of through that escape.

Verified by four `step_suite` tests written against the pages rather than the
implementation. The `BTST` test asserts the dynamic form is *not refused* rather
than that it executes, because it does not execute — `BTST Dn,#<data>` is a
named gap, reported as `UNIMPLEMENTED`, and writing the test that way means
closing the gap will not falsify it.

**1772 words remain `UNIMPLEMENTED`**, down from 2621 before C95. The largest
named remainder is family 5 — `ADDQ`, `SUBQ` and `Scc` enforce no categories at
all — and two genuinely missing instructions, `MOVEP` and `BTST Dn,#<data>`.

**The quick and ALU groups were the last two enforcing no categories**, and
closing them stopped a further **1040 words** from executing instructions the
hardware refuses. Two rule shapes that had not appeared before:

- **`ADDQ` and `SUBQ` are "alterable", not "data alterable"** — one word of
  difference from their neighbours, and it is what lets them reach an address
  register. Refusing `ADDQ #1,A0` would break code that is everywhere. They
  carry the same size restriction `TST` does, stated in the Description more
  plainly than in the footnote: "Word and long operations are also allowed on
  the address registers", so `ADDQ.B #1,A0` is illegal and `ADDQ.B #1,D0` is
  not. That is the third instruction where a *size* rule decides legality for
  one addressing mode, and no category expresses any of them.
- **The six ALU instructions state their category per direction.** "a. If the
  location specified is a source operand … b. If the location specified is a
  destination operand, only memory alterable addressing modes can be used" —
  and the source half differs between them: `ADD` and `SUB` say "all addressing
  modes", `AND` and `OR` say "only data", because an address register holds an
  address and ANDing one is not an operation the part offers.

**`Scc` deliberately has no check**, and the reason is worth recording: `DBcc`
occupies mode 001 and `TRAPcc` occupies mode 111 registers 010, 011 and 100,
which between them are every non-data-alterable mode in the group. All 800
words that reach the executor already satisfy the rule — enumerated rather than
argued. A check there would have been a second copy that never runs, the same
thing `CMPI`'s comment describes.

**And `EOR Dn,Dn` was decoded as illegal**, which the verification of the above
turned up. Four of the five memory-direction families need a *memory* alterable
destination, and that is exactly what leaves the register-destination hole for
`SBCD`, `SUBX`, `ADDX` and `ABCD`. `EOR`'s destination is *data* alterable, so
its mode-000 encoding is an ordinary and common instruction rather than a hole
to fill. `arith_suite` had a test asserting the wrong verdict, on the reasoning
that "CMP has no memory-destination form to fall back on" — the fallback is not
`CMP`, it is `EOR`.

**1468 words remain `UNIMPLEMENTED`**, from 2621 before C95. The largest named
remainder is now the bit-field group — `BFTST`, `BFEXTU`, `BFCHG` and the rest,
488 words in family E — followed by the coprocessor gaps, `MOVEP` and
`BTST Dn,#<data>`. All are open plan items; none is a category question.

**`MOVEP` and `BTST Dn,#<data>` execute**, the two instructions the opcode
sweep named as genuinely absent rather than misclassified.

`MOVEP` "moves data between a data register and alternate bytes within the
address space starting at the location specified and incrementing by two. The
high-order byte of the data register is transferred first" — it exists for 8-bit
peripherals on a 16-bit bus, whose registers land on one half of the data bus
and so occupy every other byte address. The manual is candid that it outlived
its purpose on a 32-bit part ("not useful for those processors with an external
32-bit bus"), but a driver written for the earlier one still runs. Its
addressing mode is fixed rather than decoded, so the displacement word is read
directly and no effective address is gathered.

Two details that a plausible implementation gets wrong and neither faults: the
**word form replaces bits 15-0 and leaves 31-16 alone**, so assembling the two
bytes into a long would silently clear the register's upper half; and
**"Condition Codes: Not affected"** — all of them, which is unusual enough among
the moves that setting `Z` would look right.

`BTST Dn,#<data>` is the single bit operation whose operand can be an immediate:
the dynamic form's table lists `#<data>` where the static form's dashes it, and
none of the three that *write* can reach one at all. It is handled ahead of the
address path because there is no address to gather — an immediate is a value in
the instruction stream.

The `MOVEP` test asserts the skipped odd bytes still hold the harness's `NOP`
fill rather than zero, which is the stronger statement: a zero could equally
mean they were written with zero.

**1204 words remain `UNIMPLEMENTED`**, from 2621 before C95. The bit-field group
— 488 words in family E — is now the largest single remainder by some margin.

**No opcode is declined for want of work: 2621 → 0.** Every one of the 65536
instruction words now either executes or is refused with the *machine's* own
verdict, and `machine_suite` sweeps all of them in about two seconds to keep it
that way. A new instruction landed without its refusal classified reddens that
test immediately and names the word.

The last 64 were the MMU's, and the rule is **transcribed rather than
inferred**. p. 9-51 lists what a 68030 lacks against the 68020/68851 pair —
`PVALID`, `PFLUSHR`, `PFLUSHS`, `PBcc`, `PDBcc`, `PScc`, `PTRAPcc`, `PSAVE`,
`PRESTORE`, and "`PMOVE` for unsupported registers (CAL, VAL, SCC, BAD, BACx,
DRP, and AC)" — and says they "must be avoided or emulated in the exception
routine for **F-line unimplemented instructions**". Five sites were reclassified
across `execute_pmove`, `execute_pflush_or_pload` and `execute_ptest`.

**Two of those five already had the verdict written in a comment and returned
the other one.** `execute_pmove`'s read "a register this part does not have:
F-line, not a no-op" and returned `UNIMPLEMENTED`; `execute_ptest`'s quoted the
manual outright — "The instruction takes an F-line exception when the level
field is 0 and the A field is not 0" — and did the same. That is the same shape
as `CHK`'s missing verdict in C100, and it is the fourth time this campaign that
a rule was written down correctly beside code that did not act on it.

Three `step_suite` tests had to change with them, and one could not be kept: its
subject — `F000` with a zero extension word — names a register the part does not
have, so it was never this core's gap. Its principle survives in a stronger
form, as the whole-set sweep rather than one example.

**And `CAS`/`CAS2` execute**, which this document had denied. The claim came from
a comment in `ap_m68030_step.c` reading "CAS and CAS2 still decline", two lines
above the dispatch to `execute_cas` and `execute_cas2`, and it outlived the
commits that implemented them by a long way. It was repeated here by someone
reading the comment instead of the code — a stale comment beside working code is
worse than none, because it is the version a reader trusts.

**`TRAPcc` executes**, all three forms. The operand is consumed whether or not
the trap is taken — it is part of the instruction and merely "available to the
trap handler", never used by the instruction itself, which is exactly what makes
dropping it easy and invisible. Skipping the words only when the condition is
false would run the operand as an instruction after every *taken* trap;
skipping them only when true would do so after every untaken one. Both
polarities are tested for all three forms, and the taken case checks the stacked
program counter, which is where a handler finds its data.

**Reserved coprocessor instruction types take the F-line exception.** §10.2:
"The M68000 coprocessor interface supports four categories of coprocessor
instructions: general, conditional, context save, and context restore." Types
110 and 111 are none of them. This is recorded as a **reading** rather than a
transcription — the manual defines what the four categories do and is silent on
what a fitted coprocessor does with a fifth, so vector 11 is inference from the
two neighbouring cases (an absent coprocessor, and Table 4-13's footnote 2 where
the FPCP asks for an F-line trap on an undefined *command* word). Without it
those 128 words fell through to the general path, which fetches no command word
for them — so the part was asked to execute command zero and answered about an
instruction the program had not written.

**64 words remain, and classifying them is an open item rather than a tail.**
Table 3-10 gives the 68030 five MMU operation codes; codes 5–7 and many
extension sub-patterns are undefined, and p. 8-10 makes an undefined pattern in
a subsequent word the F-line exception rather than this core's gap. That
classification was attempted and reverted: the one arm that is unambiguous broke
three tests which use operation code 5 as their example of "an instruction the
hardware executes that we have not implemented" — a premise the manual
contradicts. Landing half of it would have been the plausible-looking wrong
answer this core spends most of its care avoiding, so it is named with its scope
instead: about two dozen sites, several already commented with which kind they
are, and two questions to settle first (a new subject for those tests, and
whether `CAS.L` executes — it was observed doing so, against what this document
records).

**`MOVES` executes**, and it is the only instruction in the set that reaches an
*arbitrary* address space. Every other access this core makes carries a function
code fixed by what it is — supervisor program for a fetch, supervisor or user
data for an operand — while `MOVES` carries whatever the program last wrote into
`SFC` or `DFC`. An operating system uses it to read a user program's memory
while running in supervisor state, which no ordinary `MOVE` can do.

Modelling it as an ordinary move would work perfectly on this machine today and
be wrong the moment anything distinguishes the spaces — which is what the MMU's
function-code fields and the transparent translation registers' `FC BASE` and
`FC MASK` exist to do. Its test therefore watches the **function code the access
carried** rather than a value: flat RAM answers every code alike, so a check on
what was read could not tell a correct implementation from an ordinary move.
A supervisor-state `MOVES` with `SFC = user data` must read as user data, and
the `MOVE` beside it through the same address must read as supervisor data.

Two details from the description that no fault would reveal: an **address
register** destination is sign-extended to 32 bits where a data register
"replaces the corresponding low-order bits ... depending on the size of the
operation", and **condition codes are not affected**.

**Writing that test found a second defect.** `ap_m68030_immediate_privileged`
had no caller anywhere: the three `to SR` forms were checked by a condition
written out again inside `execute_immediate_to_status`, and `MOVES` — which the
helper also names — was checked **nowhere**. A user program could have read
supervisor memory with it. The helper is now asked once, at the top of
`execute_immediate`, so the rule is single. That is the fourth time this
campaign a declared-and-unconsulted function has marked a real gap, and the
first found by a test rather than by a sweep.

**The last three effective-address category holes are closed**, and they were
found by *naming* the remaining unimplemented words rather than guessing at
them: a sweep that reports each one's decoded kind turned "716 words" into five
named groups, three of which were categories and two of which are genuinely
absent instructions.

- **`CHK`** had the check and no verdict — it returned `false` without setting
  the refusal, so a bound in an address register reported this core's gap.
- **`JMP`/`JSR`** had no check at all, and worse, resolved the effective address
  *before* failing. Resolving applies the increment and decrement side effects,
  so `JMP (A0)+` moved `A0` on its way to reporting a gap. `LEA`'s executor has
  carried the reasoning for that ordering in a comment for a long time; the jump
  did not follow it.
- **`MOVEM`** checked that predecrement pairs with register-to-memory and
  postincrement with memory-to-register, and never checked the category at all.
  Its two directions differ in more than the increment mode: register-to-memory
  is "control alterable … or the predecrement", memory-to-register is "control …
  or the postincrement", so `MOVEM.W (d16,PC),D0` is legal and
  `MOVEM.W D0,(d16,PC)` is not — the same addressing mode, legal reading and
  illegal writing.

`MOVEM` cannot encode a data-register operand at all, mode 000 being `EXT` and
001 `EXTB`, so the cases that reach its check are the immediate and PC-relative
modes rather than the obvious one.

**420 words remain `UNIMPLEMENTED`**, and all of them are now named: `MOVES`
(180), `TRAPcc`'s operand forms (48), and the coprocessor and MMU corners of
family F (192). None is a category question; the category campaign is finished.

**The eight bit field instructions execute**, which was the largest single
block of unimplemented opcodes left — 488 words, all of family E's remainder.

A bit field is a span in a **big-endian bit stream**, not a mask applied to a
word, and three consequences follow that a word-shaped implementation gets
wrong. §1.7.2: "The MSB of the base byte is bit field offset 0; the LSB of the
base byte is bit field offset 7; and the LSB of the previous byte in memory is
bit field offset –1."

- **A 32-bit field at a non-zero offset spans five bytes.** Reading a long word
  and shifting produces the right answer for every field that fits in four and
  the wrong one here.
- **A register-supplied offset is signed** over the full 32-bit range, so the
  effective address is not a lower bound. The byte arithmetic has to *floor*:
  −8/8 is −1 and so is −1/8, where C's truncation gives 0 and puts the field one
  byte too high for every negative offset that is not a multiple of eight.
- **In a data register the field wraps and memory does not.** §1.7.1: "the
  address of the MSB is zero … If the width of the register plus the offset is
  greater than 32, the bit field wraps around within the register." One model
  cannot serve both spaces; choosing either for both is wrong in the other.

The condition codes come from the field *as found*, before any modification —
these are "test bit field and …" instructions, and `BFINS` sets them from the
field it is about to overwrite. `BFEXTS` sign-extends from the field's own width
rather than a byte, and `BFFFO` returns "the bit offset in the instruction plus
the offset of the first one bit", a position in the field's space rather than a
bit number.

Verified by four `step_suite` tests whose expectations were computed by hand
from the byte pattern `12 34 56 78 9A`: the five-byte read, the negative offset
reaching the previous byte, the register wrap (`$12345678{28:8}` is `$81`, where
clamping instead of wrapping gives `$80`), and the four writing forms — which
produce four *different* results over the same field, so a write that hit the
wrong bits could not pass more than one of them.

**716 words remain `UNIMPLEMENTED`**, from 2621 before C95. What is left is the
coprocessor and MMU corners of family F.

*(That sentence originally also named `CAS`/`CAS2` as declining. They do not:
both execute, `CAS` asserting `RMC` across its read-modify-write pair. The claim
came from a stale comment in `ap_m68030_step.c` that had outlived the commits
which implemented them, and was repeated here without checking the code it
described — see `FINDINGS.md` C103.)*

**And the machine now uses that sequence, which for a long time it did
    not.** `ap_m68030_take_reset` had no caller anywhere in `src`;
    `ap_machine_reset` ran a shorter sequence of its own -- supervisor,
    mask 7, trace clear -- which dropped steps 4, 5 and 7 and *added* an ATC
    flush, the one thing the paragraph above says reset never does. The
    rule was written down correctly here and contradicted by the code that
    ran, which is C90's shape exactly. Steps 1-7 are now
    `ap_m68030_reset_state`, shared by the exception (which reads the vector
    for steps 8-10) and by the machine (which is *told* its program counter,
    a board's PROM supplying it rather than a vector at zero).
    The omission was invisible exactly once: `ap_machine_init` zeroes the
    struct, so on a cold start VBR, CACR and every enable bit already hold
    what reset would have written, and the two sequences agree. Every later
    reset is on a machine that has been running, where a warm reset kept the
    old VBR and the old translation tree. Detail in `FINDINGS.md` C93.
    *Verification: `machine_suite`, 1 further test (30 total), which resets
    a deliberately dirtied machine. Both halves were confirmed to fail
    against the old code before being kept -- separately, since Unity stops
    at the first failing assertion and one run would have proved only one of
    them.*
    The bus and address error frames (`$A`/`$B`) build and return, and so
    does the **coprocessor mid-instruction frame (`$9`)** — which this item
    once called unreachable on the reasoning that the frame exists to resume
    a *suspended* instruction and this core's 68882 never suspends. That was
    right about suspension and wrong about the frame: Table 8-6 puts
    **main-detected protocol violation** in the same row, and that one is
    raised by the 68030 before the coprocessor is involved at all. It became
    reachable the moment the source operand transfer landed. Detail in
    `PROJECT_STATUS.md`. `CHK`, `TRAPV`,
    `TRAP`, `TRAPV`, `CHK`, the privilege violations, the illegal
    instruction word, the MMU configuration errors, the interrupts and
    **trace** are all wired in — every exception this model can build a
    frame for.
    Trace's rule is an ordering one: "The state of these bits when an
    instruction begins execution determines whether the instruction
    generates a trace exception after the instruction completes." So the
    mode is captured *before* the instruction runs, and an instruction that
    turns tracing off still traces — which is what lets a debugger step
    through the line that disables it. The stacked status register is the
    one that instruction left behind, so tracing is already off in the
    frame; the trace happened anyway.
    The change-of-flow mode counts **status register manipulations** as
    changes of flow, for a hardware reason rather than a logical one:
    "the processor must re-prefetch instruction words to fill the pipe again
    any time an instruction that can modify the status register is
    executed". A model tracing only actual branches would silently skip
    every `MOVE to SR` a debugger asked to see.
    An illegal or unimplemented instruction is **not** traced, "since it is
    not executed" — the distinction an emulation routine depends on, since
    it must raise the trace itself and would otherwise trace twice. And "if
    an instruction forces an exception as part of its normal execution, the
    forced exception processing occurs before the trace exception is
    processed", so a traced `TRAP` stacks *both*, trace on top, and the
    trace handler runs first and returns into the trap's.
    **Table 8-6's bracketed column is a second fact, separate from the frame
    size**: the stacked PC is the *next* instruction for an interrupt, a
    `TRAP` and everything in the six-word row, but the *faulting*
    instruction for illegal instruction, A-line, F-line, privilege violation
    ("First word of instruction causing Privilege Violation") and format
    error ("RTE or cpRESTORE instruction"). Defaulting to "next" has a
    privilege-violation handler emulate the instruction and return past it,
    and a format-error handler return past the `RTE` it was called to
    diagnose. Both run; neither faults.
    *Verification: `step_suite`, 10 further tests (89 total) — the stacked
    SR being the pre-change copy, tracing turned off, the frame on the
    supervisor stack with the USP untouched, the vector fetched through the
    VBR at offset rather than number, the six-word frame carrying two
    different addresses, the unbuildable frames declined with the stack
    pointer unmoved, and the handler's first instruction actually executing
    next; `exception_suite`, 2 further tests (16 total) sweeping Table 8-6's
    rows including every TRAP and every autovector. Then probes that
    deliberately fault, diffed against the oracle, which is what this item
    always asked for.
- [x] 68030 on-chip MMU: translation tables, ATC, transparent translation,
  `MMUSR`. *Verification: probe walks and faults; oracle diff.*

#### Instruction execution time: the microcode clocks

    cycles.** **Closed.** Every transcribed row is priced as
    `microcode + measured operand bus + prefetch cost`, where the microcode
    is `CC − 2(r + w)` from the published `(r/p/w)` and the bus half is what
    this core measures -- so a wait state or a cache hit still moves the
    answer. `FINDINGS.md` C9's row, `ADD.B D0,(A0)`, comes to 6 warm and 7
    cold averaged over both alignments: the manual's composed figure and the
    oracle's measurement. What remains under it are two *readings* rather
    than gaps, both named below. Named here because it was missing from this plan entirely,
    which is worse than being open: the step accumulates bus and cache time
    only, so a register-to-register `ADD` costs **zero** clocks today and
    every figure the core reports is a lower bound. The core's headline
    claim is emergent timing, and this is the part of it not yet built.
    **Not to be closed by transcribing §11.6's NCC column** — that one is a
    mean of the odd- and even-aligned cases, rounded up, so a core adding it
    would reproduce an average the hardware never exhibits.
    **But the `CC` column is a different quantity, and it is the one we
    need.** `docs/references/M68030_TIMING.md` now records the distinction:
    §11.6's legend gives each entry as `total(reads/instruction-bus/writes)`,
    and a register-to-register form reads `CC 2(0/0/0)` — no operand reads,
    no writes, and no instruction bus cycles, because the instruction is in
    the cache. There is nothing in that number but microcode, and §11.3.1
    defines CC without any averaging. So the route here is the project's
    other permitted one: **a documented value with a cited page**, not a
    measurement.
    The same legend independently confirms C7, saying the published
    prefetch count "is always greater than or equal to the actual number of
    bus cycles (one bus cycle per two instruction prefetches)" — the actual
    rule being exactly our alternation.
    The route is the project's own rule: measure against the oracle, or
    take a documented value with a cited page and mark it `PROVISIONAL`.
    **The route now exists on both sides** — see `FINDINGS.md` C5. Our
    machine side-loads and steps (`ap_machine`, `ap_probe`); the oracle
    does too, with `-debug -debugger none` making `cpu.debug:step()`
    available headlessly, and Lua reading and writing memory and registers.
    Neither needs firmware, so C4's PROM problem no longer gates this.
    Both of C5's prerequisites are now **settled** — `FINDINGS.md` C6 and
    `tools/mame-oracle/steptime.lua`. Time advances in whole cycles: every
    `NOP` step is exactly 80,000,000,000 attoseconds against a 40 ns clock,
    and the script prints the raw attoseconds beside the derived count and
    flags any remainder `FRACTIONAL` rather than rounding it away. The
    side-load guard reads the probe back and refuses if it disagrees, which
    is what stops a harness aimed at the PROM's range measuring the PROM
    while reporting the probe.
    First four measurements, oracle-side: `NOP` 2, `MOVEQ` 2,
    `ADD.L D0,D1` 2, `LSR.L #1,D0` 4 clocks.
    **The first comparison is done, and it settles the shape of this item**
    — `FINDINGS.md` C7. Our bus time *alternates* 0/2 per instruction where
    the oracle is a flat constant, and the manual says ours is right:
    §11.3.3 computes the no-cache case "assuming ... one external bus cycle
    per two instruction prefetches", which is the cache holding register
    serving two instruction words per fetch, and then says the published
    figure is "the **average** of the odd-word-aligned case and the
    even-word-aligned case". The oracle's flat number is what a cycle-table
    model produces; ours exhibits the alignment dependence the manual calls
    real. Classified `oracle-wrong`, ours kept.
    **So the target for this item is not a per-instruction constant.** It is
    a microcode time added to a bus time that keeps alternating, and the
    check is that our average over both alignments matches `[030]` §11.6 —
    not that our per-instruction figure matches MAME's. That the same 0/2
    appears for `LSR.L #1,D0` as for `NOP`, where the oracle charges 4
    against 2, is precisely the execution time this item is about.
    Both halves of the comparison are now runnable and pinned:
    `apollo-headless --time-instructions` with a golden, and
    `tools/mame-oracle/steptime.lua` for the oracle.
- [x] **Wiring the figures in is a *scheduling* problem, not an addition**,
      and the scheduling model now exists: `ap_m68030_schedule`, which is
      `max(microcode, bus)` and is applied to every transcribed form.
      The three transcribed instructions in `--time-instructions` went from
      alternating 0/2 to a **steady 2**, which is what the manual predicts:
      `MOVEQ` is `CC 2` and `NCC 2`, the same figure cached or not, because
      its two-clock fetch hides entirely under its two clocks of microcode.
      The untranscribed ones still alternate, visibly bus-only.
      **A test had to be restated rather than repaired.** One asserted that
      a second pass over cached instructions costs *zero*, which was right
      while the clock was bus time alone. It now costs the published `CC`,
      and — because `CC` equals `NCC` for `MOVEQ` — costs the *same* as the
      uncached pass. So what the cache buys there is bus cycles, not clocks,
      and the test says that instead: no further line fills, and four
      `MOVEQ`s costing 8.
      It remains a **two-resource approximation** of §11.2's eight, kept
      because it reproduces both published columns for every transcribed row
      and because the alternative is inventing structure the manual does not
      publish. The remaining work is the two-sided check across more rows:
      cold-cache totals equalling `NCC`, warm-cache totals `CC`. A row where
      they stop agreeing is where this runs out, and is a measurement worth
      having.
      *Verification: `overlap_suite`, 4 further tests (12 total) — a bus
      cycle wholly hidden costing nothing, bus time beyond the microcode
      being what costs, a 44-clock divide swallowing its fetch, and an
      untranscribed instruction remaining its bus time. Both goldens moved
      and were regenerated, byte-identical between `-O0` and `-O3`.*
- [x] **The original addition, implemented and backed out.**
      Detail in `PROJECT_STATUS.md`.
      *Verification: per transcribed row, cold-cache total equals `NCC` and
      warm-cache total equals `CC`, both from `[030]` §11.6.*
- [x] **The published figures, transcribed**
      (`src/core/cpu/m68030/ap_m68030_timing_table.c`). Nineteen rows from
      §11.6.8 and §11.6.9 whose instruction-cache case reads `n(0/0/0)` —
      no operand reads, no writes, and no instruction bus cycles because the
      instruction is in the cache, so `n` is pure microcode. The `NCC`
      column is not transcribed at all, and neither are the rows with a
      non-zero read or write count: their `CC` includes operand bus cycles
      this core produces itself, so adding them whole would count twice.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `timing_table_suite`, 7 tests — a transcription cannot
      be checked by re-reading it, so these check it against structure:
      every row satisfying the head ≤ CC rule a dropped or doubled digit
      usually breaks; the word-address-forms-cost-4 pattern that showed
      §11.3.4's example to be mislabelled, pinned so the table cannot drift
      back towards it; the seven register operations agreeing with each
      other, so one mistyped row stands out; the divides marked and **only**
      the divides, since a marker applied too widely makes every figure look
      provisional; what is not transcribed reported absent rather than
      approximated by a neighbouring row; and the figures composing through
      Equation (11-1), which is what they were transcribed for.*
- [x] **The composition rule, built without the numbers**
      (`src/core/cpu/m68030/ap_m68030_overlap.c`). Equation (11-1) and
      §11.2's eight resources are arithmetic and vocabulary rather than
      measurement, so they land now and the figures have somewhere to
      arrive.
      Detail in `PROJECT_STATUS.md`.
      *Verification: `overlap_suite`, 8 tests — §11.3.4's **worked example**
      checked verbatim at 6 clocks, which is an external number rather than
      one of ours; the directional pairing; an instruction fully absorbed
      costing nothing; the first instruction overlapping with nothing;
      overlap being pairwise rather than cumulative across three
      instructions; and a head or tail longer than its instruction reported
      inconsistent, since such an entry was mis-transcribed.*
    *Verification: self-timing probes against the oracle, per instruction
    and per addressing mode, with `[030]` §11.6 as an independent check and
    every discrepancy classified before anything is changed.*

#### The 68030 instruction step

fetch through the pipe and instruction cache, decode, execute, advance
the PC, account the clocks. **A program runs.**
Executing today: **everything in families `0000` through `1111`**
except `BKPT`, `CAS`, `CAS2`, `CMP2`, `CHK2` and the coprocessor
instructions other than the MMU's. Every addressing mode the part has
resolves, full-format indexed and memory indirect included. Exceptions
are taken *and returned from*, including bus error, address error and
the line 1010 and 1111 emulator traps.

(This paragraph read "`NOP`, `MOVEQ`, and the 8-bit forms of `BRA` and
`Bcc`" until it was corrected. That was true when the item was written
and had been false for a long time — the tables below were updated
commit by commit and the prose describing them was not, which is the
same rot found in `PROJECT_STATUS.md`'s "no CPU" line.)
**Unimplemented is a distinct outcome from illegal**, and that is what
lets this ship incomplete. Silently doing nothing would make a program
appear to run while producing wrong results *and* a wrong clock count;
reporting illegal would be a lie about the hardware and would send a
probe down an exception path the real machine never takes. So an
unimplemented instruction stops the step, says so, and leaves the PC
where it was — "how far did this program get" is then a real measure.
`-Wswitch-enum` keeps it honest: every decoded kind is listed
explicitly, so adding a family to the decoder forces a decision here
rather than letting a `default` make it silently.
*Verification: `step_suite`, **178 tests** — including `MOVEQ` sign-extending `$FF` to
−1 and setting exactly the documented condition codes (with X asserted
to *survive*), a four-instruction program running to its end, `BRA`
landing on its target and the instruction there being the expected one,
a conditional branch reading the flags the previous instruction set —
the first interaction between two instructions — an unimplemented
instruction reported rather than skipped, an illegal encoding distinct
from it, and a second pass over the same code costing **zero** clocks
because the instruction cache answers.*

**What stops it being `[x]`**, and why each is a *reason* rather than an
omission:
- `BKPT` **now executes**, running its breakpoint acknowledge cycle in
  **CPU space** per §7.4.2. The breakpoint number rides on **A2-A4**,
  not on the low address lines: putting it elsewhere acknowledges a
  different breakpoint, and external hardware answers with the wrong
  instruction word rather than faulting.
  On a DN3500 nothing decodes CPU space, so the cycle takes a bus error
  and "the processor takes an illegal instruction exception" -- the
  machine's behaviour, not an error path, which is why declining was the
  wrong report.
  *Verification: `step_suite`, 2 further tests (191 total) -- the
  illegal instruction taken when nothing answers, and the acknowledge
  cycle's address and function code checked directly, since a wrong
  breakpoint number is a legal address that no fault would reveal. The
  harness records the **first** CPU-space cycle, because the vector
  fetch that follows would otherwise be the one pinned.*
- `CAS` **now executes**, and the bus asserts `RMC` across the pair.
  The signal lives on the access context rather than on a bus object,
  because each access creates its own cycle and the signal spans two of
  them. §7.3.6's "the burst mode is never used during read-modify-write
  cycles" is enforced at the request rather than at the acceptance, so
  `CBREQ` is never raised inside one.
  *Verification: `bus_suite`, 2 further tests (25 total) -- `RMC`
  surviving the cycle boundary inside an operation, and a burst refused
  with `CBACK` offered anyway so the refusal is not cosmetic.
  `step_suite`, 4 further tests (189 total) -- the swap on a match; the
  **write that still happens on a mismatch**, going the other way into
  the compare register, which a model that skipped the store would turn
  into a retry loop that spins forever; and the lock released on both
  outcomes, since `RMC` held past the instruction would refuse every
  later bus grant.*
- `CAS2` **now executes too, and the reason it had been declined was a
  misreading.** Its addresses come from *registers* -- "Rn1, Rn2 fields:
  specify the numbers of the registers that contain the addresses of the
  first and second memory operands" -- not from an addressing mode. The
  `<ea>` in the operation word is the immediate encoding used purely as
  an escape, and reading it as an address is what made the instruction
  look like something the operand path could not express.
  Both comparisons happen before either write, so a failure leaves
  memory untouched rather than half updated -- which is the corruption
  the instruction exists to prevent. And the two register writes go in
  **reverse** order, because "if Dc1 and Dc2 specify the same data
  register and the comparison fails, memory operand 1 is stored in the
  data register": operand 1 must be the one that remains, so it is
  written last. The obvious order leaves operand 2 there, and only in
  the colliding case, which no ordinary test reaches.
  *Verification: `step_suite`, 4 further tests (196 total) -- both
  operands swapped or neither, nothing written when the *second*
  comparison fails, the colliding-register case, and the lock released.*
- **Nothing in the step is unimplemented now.** The suite's
  unimplemented-instruction placeholder has passed from BKPT to `CAS2`
  to an undefined MMU extension class, each of the first two having been
  implemented in turn -- which is what the placeholder is for.
- `CMP2` and `CHK2` decode and have no semantics yet. **Not blocked** —
  simply not done, and the smallest remaining piece of this item.
- The non-MMU coprocessor instructions take the line 1111 emulator trap,
  which is **correct** on a machine with no coprocessor fitted rather
  than a gap. If an FPU is ever modelled they become real work.

**All four are now done or reasoned.** `CMP2`/`CHK2` execute, `CAS`
executes with `RMC` asserted across its pair, `BKPT` executes its
acknowledge cycle, and the non-MMU coprocessor instructions take the
line 1111 trap, which is correct on a machine with no coprocessor
fitted. `CAS2` executes as well — the paragraph above records why the
reason it had once been declined was a misreading, and this sentence is
kept in its corrected form rather than deleted so the two readings stay
visible together.

#### The narrow build itself (`SUBTARGET=apollo`, `REGENIE=1`, `NOWERROR=1`)

Built and running: MAME v0.289, one driver, no tools.

#### **Answered: the 68040 path does have an oracle.**

`dn5500` starts and dumps reproducibly under the built binary (985
bytes, two runs byte-identical) *despite* `MACHINE_NOT_WORKING`. So the
flag is not grounds to move DN5500/DSP5500 to `paper` in the model
table, and Phase 2's "`dn5500` oracle diff" stands as written — but the
flag is MAME's own statement that it does not vouch for the result, so
DN5500 readings are a divergence class to treat with suspicion rather
than a missing oracle. Recorded in `FINDINGS.md`. The original wording
of this item is kept below, because the reasoning it rejected — acting
on the flag alone — is the part worth remembering.
*Original:* **Verify empirically whether the 68040 path has an oracle at all.**
`apollo.cpp` declares `dn5500`, `dsp5500` and `dn5500_19i`
`MACHINE_NOT_WORKING` while no 3000 or 3500 machine carries the flag. If
it holds, Phase 2's "`dn5500` oracle diff" is unachievable as written and
DN5500/DSP5500 move from `mame` to `paper` in the model table (which is
a golden change). Not acted on from the flag alone — the honest test is

#### The remaining half: `verify` against the **real** oracle, showing two

Both were concealed by a report that echoed its own input.
- Determinism is the whole point, so the driver's flags are load-bearing and
each closes one way a second run could differ: `-noreadconfig` (ignore
`~/.mame/mame.ini`, which no one reviews), redirected and wiped
`nvram`/`cfg`/`state`/`diff` directories (they persist across runs by
design, so run 2 would otherwise start from a different machine than run 1),
`-video none -sound none -nothrottle`, and `-seconds_to_run` as an
emulated-time watchdog so a never-reached dump point fails instead of
hanging.

#### **The output format: captured, byte-exact.** The handbook never shows a

`docs/references/MD.md` now records the bytes: the `CR LF` terminator,
the blank line before each prompt, the bare `>` prompt, the `A`
command's address field and its width rule, and the full sign-on

#### **A defect in the state hash, found by building on it.** Two machines

`ap_m68030_cache_clear` clears valid bits and leaves the tag and data
behind — by design, and documented — so an invalid entry holds whatever
the last occupant left, which no lookup can reach. The hash fed it
anyway, and `ap_m68030_atc_flush` leaves the same debris.
That is a **false positive**, and worse than a miss: a harness that
rejects identical machines cannot be used at all, so every item gated on
it would have been gated on something unusable. The hash now covers what
an access can reach — a line's tag only when some entry is valid, an
entry's data only when that entry is — with one deliberate exception,
the ATC's history bit, which the replacement algorithm reads whatever the
valid bit says.
`ap_machine_init` also now defines *every* field rather than the ones it
sets: a machine whose behaviour depended on what was in the caller's
stack beforehand is not reproducible, which is the one thing it must be.

#### **The CPU's contribution** (`src/core/cpu/m68030/ap_m68030_state.c`)

The instruction and data sides are fed in that order, so a machine with
the two caches exchanged does not hash the same as one without; an
absent access context feeds a marker rather than nothing, since "no data
side" and "a data side whose cache is empty" are different machines; and
an *invalid* ATC entry still contributes its history bit, which is what
the replacement algorithm reads.
The failure mode this must not have is a field that moves while the hash
does not — the harness would then report two diverging machines as one.
So `state_suite` **sweeps every field individually**: perturb it, and
the hash must change. A field added without a sweep entry is a gap
visible in that file rather than one nobody can see.

#### **The board half of the state hash**

(`src/core/board/ap_board_state.c`), which closes Phase 1's last item.
The CPU's half had been done for months; what was left was everything on
the other side of the bus, and it could not be written until there were
devices to write it about. Now there are: the board registers, the
address translation map, both interrupt controllers, the interval timer
with its three clock domains, the calendar with its separate one-second
and periodic cursors, both DMA controllers, both serial ports, the node
ID, the disk and tape controllers, the graphics memories, the keyboard
matrix and the boot PROM.

**The line that had to be drawn is between state and instrumentation.**
`ap_board_t` carries both, side by side: the devices are the machine, and
the counters beside them — `unmapped_reads`, `region_writes`, the
per-register serial tallies, and `ap_machine_t`'s own `bus_errors` — are
our record of *watching* it. Only the first kind is hashed, and the
choice cuts both ways deliberately. A counter inside the hash would make
adding an instrument change every golden with no emulated behaviour
changing, and would make two machines that behave identically compare
unequal because one was watched more closely — a false positive, and by
the same argument as the cache-debris defect above, worse than a miss.
Nothing is lost by leaving them out: `ap_machine_state` reports them
beside the number, and `tests/goldens/probes.txt` already pins the
bus-error count as its own column, where a divergence reads as a figure
rather than as a hash that merely differs.

That rule moved `bus_errors` *out* of `ap_machine_hash`, where it had
been since before there was a board. The probe golden's hash column moved
with it and **every other column did not** — instructions, status, D0,
PC, clocks and the bus-error count are unchanged in all eight probes,
which is what says the definition changed and the machine did not.

**Two more exclusions, each with a reason rather than an omission.** Main
memory's contents are hashed by `ap_machine_hash` and not again here: the
board and the machine share one buffer and hashing megabytes twice buys
nothing. Its *extent* is hashed, since 8 Mbyte fitted is a different
machine from 16 however well the bytes in common agree. And the tape
cartridge is hashed by extent alone — a `.ct` image is up to a hundred
megabytes of read-only media no run can alter, so re-reading it on every
hash would cost more than the run it measures. Everything a run *can*
change is hashed in full: which block is buffered, where the head is,
what the drive was told. The residual is named in code and here — two
different cartridges of exactly equal size hash alike until one is read,
and closing it needs a digest computed once at load time.

The graphics memories are the opposite case and **are** hashed in full.
Nothing else covers them, since they hang off the display controller
rather than off the machine, and they are the machine's output: a run
that drew a different picture is a different run.

**Elapsed time joins the hash.** The CPU's clock count was already hashed
with its registers; `ap_machine_now` and the CPU's clock rate are now
hashed too, and they are not the same quantity — two machines that reach
identical processor state at 25 MHz and at 20 MHz have taken the same
cycles and different amounts of time, and once a device advances on a
clock of its own that difference is where a fast mode would diverge.
`machine_suite` checks exactly that pair: the processor's own hash agrees
and the machine's does not.

**And the numbers beside the number.** A hash says whether two runs are
the same and nothing about where they parted, so `ap_machine_state`
returns the clock, the elapsed time, the PC and the bus-error count with
it, and the headless frontend prints the block rather than the hash
alone. That is the plan item's "with emulated cycle count and PC reported
beside it", and it is what turns a disagreement into a bisection.

Printing `elapsed` immediately showed it reading **zero**, because no
boot path had ever set the machine's clock rate — which is the behaviour
`ap_machine.h` promises for a rate nobody chose, and is why the default
is no time at all rather than a plausible number. The boot paths now take
the rate from the **model table** (`ap_model_by_name("dn3500")->cpu_hz`),
not from a constant in the frontend: `ap_board` is the DN3500's core
board, so a boot through it is a DN3500 run, and taking the figure from
the table keeps `CLAUDE.md`'s "all machine variance lives in
`src/core/model/`" true of the frontend as well as the core. A 20000
instruction PROM run now reports 75880 clocks and 20,032,320 base units,
which is exactly 264 per clock — 6.6 GHz over 25 MHz, an integer, as
`ap_clock_init` refusing to round guarantees.

The failure this must not have is the CPU half's: a field that moves
while the hash does not. `board_state_suite` therefore sweeps **every
field of every device individually** — 22 tests, most of them loops over
a device's members — and the sweep is per *field*, not per device,
because a device fed as a whole struct passes a per-device test while
quietly omitting half its members. That is how a hash goes hollow.

#### **Both timing compositions, from the manual's own worked examples**

The instruction execution time item's arithmetic is now verified on both
sides, and the reasoning is in `docs/references/M68030_TIMING.md`. Three
results, each from going back to §11.3.3 and §11.3.4 in the PDF rather
than to the notes this project had accumulated about them.

**Equation (11-2) is Equation (11-1) over *components*.** The manual
prints it as a separate, "more specific" formula for the instructions
whose effective address time comes from another table. Every term has the
same shape — a component's cache case less the lesser of its own head and
the *previous component's* tail — and the only thing (11-2) adds is that
an instruction contributes two components, its effective address then its
operation. So there is one rule, and `ap_m68030_overlap_add_component`
now sits beneath `ap_m68030_overlap_add` with
`ap_m68030_ea_timing_compose` adding the pair. Writing (11-2) as its own
accumulator would have duplicated the rule and left two places for it to
drift.

Verified against the manual's five-instruction example, which exists
precisely to exercise (11-2) and prints its answer: **40 clock periods**.
The components are fed in *as the example prints them* rather than read
from our tables — feeding the transcription in would check the
composition against our own numbers, and a mistranscribed row would move
both sides of the comparison together. A separate test then checks four
of our rows against the same example, which is a check against a
different page from the one they were transcribed off.

**A register operand contributes no component at all**, not one costing
zero. The example's last instruction, `NEG D3`, overlaps against the
*previous operation's* tail, reaching past where an address component
would have been. A zero-cost component there costs nothing itself and
still consumes that tail, over-counting by up to its length. The tables
say the same thing by writing a register row's head and tail as `-`
rather than 0, which is why `head_applies` was carried from the first
transcription; this is the first thing that needed it.

**The no-cache case composes by plain addition**, per §11.3.3's "2 + 7 =
9 clocks" and "9 + 7 = 16 clocks", so the two published columns compose
by two different rules. `ap_m68030_no_cache_total` is the second, kept a
separate function deliberately: running `NCC` figures through head and
tail would subtract an overlap the published figure already excludes.

**And the finding that decides what this core can ever be compared
against.** §11.3.3 works an instruction costing "eight clocks for even
alignment and 10 clocks for odd alignment, an average of nine", while the
*pair* it belongs to costs "16 clocks for both even and odd alignment".
The alignment difference therefore moves *between adjacent instructions*
rather than adding to the stream: whichever of 8 or 10 the MOVE costs,
the CMPI after it costs 8 or 6 to match. So a published `NCC` is not
merely an average — it is a figure the hardware never exhibits for that
instruction, and the right unit of comparison is a **sequence**, where
the alternation cancels. This core already exhibits exactly that
alternation (`FINDINGS.md` C7), which had been classified as the oracle's
error; §11.3.3 is the manual saying so directly.

#### **The decomposition, and `FINDINGS.md` C9 closed**

`CC` and `NCC` both contain the instruction's own operand cycles, which
is why `CC + bus time` over-counted. §11.6 states both halves of the
split at the head of every table — the `(r/p/w)` counts "are included in
the total clock cycle number", and "all timing data assumes two-clock
reads and writes" — so

```
  microcode = CC - 2(r + w)
```

is arithmetic on published numbers rather than a model. `r` and `w` are
now transcribed beside `p`, and the step prices a row as

```
  total = microcode + measured operand bus + prefetch cost
```

The microcode comes from the manual; the operand bus is what this core
measured, so a wait state, a cache hit or a slow device still moves the
answer. That is the whole difference between this and a cycle-table
model, and the reason the figures were decomposed rather than used whole.

**`ADD.B D0,(A0)` now costs 6 warm and averages 7 cold** — the manual's
composed figure and the oracle's measurement, which C9 has been open on
since it was first measured at 4 here. Our cold figure alternates 6 and 8
with prefetch alignment where the oracle is flat; the average agrees, and
that alternation is C7's classification standing rather than a residual
disagreement.

**The marginal cost of a prefetch, recovered for the rows where it is
recoverable.** The withdrawn `(NCC−CC)/p` claim was missing not
arithmetic but §11.3.3's definition of `NCC` as an average over the two
alignment cases. For a single-word instruction that is not a change of
flow, the odd-aligned case runs no fetch at all, so the even case is
twice the published difference — and comes to 0 or 2, meaning such a
prefetch either hides completely under the microcode or not at all.
`timing_table_suite` computes that over every row of the class and it
survives.

Three things the wiring forced, each found by a number moving that should
not have:

- **The `*` and `**` footnotes name different tables**, and could no
  longer share a flag once one of them could be priced. `**` is §11.6.2,
  not transcribed; those rows still decline rather than being priced off
  §11.6.1 — a plausible number from the wrong page.
- **The exposure rule was being applied to rows it was derived to
  exclude.** The test named them and the step did not. Applicability is
  now data on the row, following from the instruction's length in words
  and whether it changes flow, because it belongs where the figure is
  *used* and not only where it is checked.
- **A pipe refill is not the row's own prefetch.** Substituting a
  published exposure for measured instruction-bus time is valid only for
  the one cycle that keeps a full pipe full; §11.6 charges a refill to
  the branch that caused it. This core charges it where it happens, which
  shifts cost between adjacent instructions rather than changing the
  total — exactly what §11.3.3 says alignment does, and why it reports a
  stable 16 clocks for a pair whose members are 8 or 10.

**Still open**, each named: §11.6.1's and §11.6.3's full-format extension
word rows, without which nothing composes over a memory indirect mode;
§11.6.2 for the `**` rows; and the change-of-flow rows' prefetch cost,
declined because the target's alignment decides the count, leaving their
warm figures exact and their cold ones a lower bound.

#### **Instruction pipe and cache holding register**

This is the mechanism §11.3.3 averages over when it publishes a
no-cache-case time, so modelling it is what lets this core produce the
per-run number instead of the published mean.

#### **Programming model** (`src/core/cpu/m68030/ap_m68030_regs.c`), `[030]`

**A7 names one of three registers**, and in user state M is *ignored*
rather than required to be zero: the PRM's table reads `S 0, M x → USP`,
`1 0 → ISP`, `1 1 → MSP`. Switching on the pair as four cases invents a
fourth stack. This is the 68020-and-later addition — on the 68000 "the
M-bit is always zero" and there is one supervisor stack — so it is
precisely what a 68000-shaped model gets wrong.

#### **Conditional tests** (`src/core/cpu/m68030/ap_m68030_cond.c`), the

**The table's overbars do not survive the scan** — `HI` reads as "C^ Z"
where the manual means not-C and not-Z — so this is the first table
whose *content* is damaged rather than its layout. It is recovered from
structure rather than guessed: the encoding lays the conditions out in
complementary pairs (`2k` against `2k+1`), so a misplaced bar must make
some pair agree for some CCR value.

#### **Operation code map** (`src/core/cpu/m68030/ap_m68030_opcode.c`)

**The MOVE families are not in size order**: `0001` byte, `0010`
**long**, `0011` **word**. Assuming byte/word/long yields a decoder that
runs and moves the wrong number of bytes for two thirds of all MOVE
instructions, which is why the size is exposed here rather than left for
each caller to re-derive — there is no arithmetic on the family number
that produces it.
Families `1010` and `1111` are named rather than lumped in as invalid,
because they are exception *generators*: `[030]` Table 8-1 gives vector
10 to the "Line 1010 Emulator" and 11 to the "Line 1111 Emulator", which
is what lets an OS emulate an absent coprocessor in software. Those
vector numbers come from `ap_m68030_exception.h`, so the two modules
agree by construction rather than by two copies of a constant.

#### **Branch family decode** (`src/core/cpu/m68030/ap_m68030_branch.c`)

**The branch base and the BSR return address are different addresses.**
The PRM gives the branch as "PC + dn → PC" with PC the instruction
address *plus two*, while BSR pushes "PC" meaning the instruction that
follows — a whole instruction length away. They coincide only for the
8-bit form, which is exactly why the mistake survives casual testing:
computing the return address as the base gives a BSR that returns into
its own displacement words for the 16- and 32-bit forms.
`$00` and `$FF` are escapes rather than displacements, so the 8-bit
field cannot encode 0 or −1 — and the manual's own NOTE gives the
visible consequence: "A branch to the immediately following instruction
automatically uses the 16-bit displacement format".

#### **Family `0101` decode** (`src/core/cpu/m68030/ap_m68030_quick.c`):

Neither it nor `TRAPcc` is a special case bolted on; both are holes in
`Scc`'s own address space.
Two traps, each tested. The quick data field's **zero means eight** —
a decoder passing it through turns every add-8 into an add-0, an
instruction that runs, sets condition codes and does nothing. And
`DBcc` terminates on **−1 after decrementing**, so a starting count of
zero decrements to `$FFFF` and stops after one pass rather than
wrapping to 65535; zero is explicitly *not* the terminator, which is
asserted directly.

#### **The `$4E` control group**

**TRAP's four-bit field is an index, not a vector number**: Table 8-1
puts TRAP #0-15 at vectors 32-47, so returning the field itself sends
TRAP #0 to the reset vector. The vector comes from
`ap_m68030_exception.h`, so the two modules agree by construction.
**Four are privileged** — RESET, STOP, RTE and both directions of MOVE
USP — and this is a class whose failure mode is silent: a user program
able to execute them could halt the processor or forge a return from
exception. The test states the distinction that matters, that RTS and
RTR are ordinary while RTE is not.

#### **The LEA/CHK and `$48`/`$4C` subtree**

**The same trick appears three more times here**, and getting the
decode *order* wrong is how it bites: an instruction that takes only
some addressing modes leaves holes, and other instructions live in
them. `PEA` cannot push a register, so mode `000` there is `SWAP` and
`001` is `BKPT`. `MOVEM` moves registers to or from memory, so mode
`000` is `EXT`. `LEA` loads an address, so mode `000` under its opmode
is `EXTB.L`. In each case the register-direct form must be recognised
*before* falling through, and a decoder checking the wider instruction
first produces a working instruction doing the wrong thing.
`EXT`'s encoding also fixes bits 11-9 at `100`, which is `MOVEM`'s
registers-to-memory direction — the other direction has no `EXT` at
all, so a data register operand there is invalid rather than a third
`EXT` form.

#### **The single-operand group**

Size field `11` is an escape rather than a size, and what it escapes
*to* differs per row — `$40C0` is `MOVE from SR`, `$42C0` `MOVE from
CCR`, `$44C0` `MOVE to CCR`, `$46C0` `MOVE to SR`, `$4AC0` `TAS`. So
the bit pattern meaning "long" one row up means an entirely different
instruction here. That is the third distinct place in the encoding
where an illegal size selects something else, after `ADDQ`'s
conditional group and `Bcc`'s displacement escapes, and it is worth
treating as a family idiom rather than five special cases.
`ILLEGAL` (`$4AFC`) is a *defined* word inside TAS's range, not an
absence of one — its purpose is to take the illegal instruction
exception.
Privilege reads backwards and is tested as such: `MOVE to SR` is
privileged because it writes the S bit, `MOVE to CCR` is not; and
`MOVE from SR` became privileged on the 68010 (a user program that can
read S learns whether it is supervised) while `MOVE from CCR`, which
the 68000 lacked, is unprivileged.

#### **Correction: `$4E7A`/`$4E7B` are MOVEC, not unassigned.** The `$4E`

Both the decoder and the test that endorsed the error are fixed, and
MOVEC is privileged along with the rest.

#### **MOVE and MOVEA decode** (`src/core/cpu/m68030/ap_m68030_move.c`)

**The destination field is reversed**: source is `MODE` then `REGISTER`
as everywhere else, destination is `REGISTER` then `MODE`. A decoder
reading them the same way round gets a plausible wrong instruction
rather than a fault, and the suite pins the reversal in a single
comparison — the same bits are the immediate mode at `$29C0` and
absolute short at `$21C0`.
The size comes from `ap_m68030_opcode_move_size` rather than a second
table, because these bits *are* the low half of the family number; a
local copy could disagree with the map.
`MOVEA` is not a separate encoding but the one destination mode that
changes behaviour — it does not affect the condition codes, which is why
it is worth distinguishing — and there is **no byte MOVEA**, so a
byte-sized address register destination is not an instruction.

#### **Family `0000` decode** (`src/core/cpu/m68030/ap_m68030_immediate.c`):

**MOVEP is the strongest instance yet of this encoding's idiom.** It
uses *the same four opmodes* as the dynamic bit operations and is
separated only by the effective address mode: a bit operation cannot
address an address register, so mode `001` there is MOVEP. The overlap
is **total** rather than partial — there is no opmode that is MOVEP and
not also a bit operation — which the suite checks by walking all four.
The `to CCR`/`to SR` forms are likewise not separate opcodes but the
immediate-*destination* encoding, meaningless as an address, with the
size field choosing byte (CCR) or word (SR). SUBI, ADDI and CMPI have no
such forms, so for them that encoding stays unassigned rather than
aliasing — also tested.

#### **The arithmetic and logic families**

Five families with one shape, which is why they are one module —
`family | register | opmode | effective address`, with opmodes `000`-
`010` the register direction, `100`-`110` the memory direction, and
`011`/`111` the wide forms.
**The wide opmodes share a position but not a meaning**: `011` is a word
`DIVU` in family `1000`, a word `MULU` in `1100`, and a word `SUBA`,
`CMPA` or `ADDA` in the other three. The suite asserts all five at the
same opmode, since a decoder assuming one shape for all of them gets
four families wrong.
The memory-direction opmodes leave register-direct **holes**, filled
differently per family — SUBX, ADDX, ABCD, SBCD, CMPM and EXG — the same
idiom as SWAP inside PEA, now in five families at once. Tested against
the ordinary instruction at the same opmode with a real memory mode, so
the holes are shown to be holes rather than special cases.
`CMP` and `EOR` share family `1011` **without overlapping**: CMP has the
register direction, EOR the memory one, and there is no
`<ea> EOR Dn -> Dn` form at all — asserted as the absence it is.

#### **Family `1110` decode** (`src/core/cpu/m68030/ap_m68030_shift.c`):

One family, three shapes. Bits 7-6 other than `11` is a register shift;
`11` is not a size, and bit 11 then chooses between a memory shift (one
word, by one) and a bit field instruction. That is the *fourth* place in
the encoding where an illegal size selects something else.
**The type field moves between the two shift forms**: bits 4-3 in the
register form, bits 11-9 in the memory form — where the register form
keeps its shift count. Reading one position for both gives a working
shift of the wrong kind, so the suite checks all four types in each
position and asserts that the same bits are a *count* in one form.
The immediate count's **zero means eight**, the same quirk as `ADDQ`'s
quick data — but only when `i/r` is clear; with it set the field is a
register number where zero means register 0, so the substitution must
*not* happen. Both directions tested.

#### **Family `1111` decode** (`src/core/cpu/m68030/ap_m68030_coproc.c`):

cpID 0 is the 68030's *own MMU*: "The MMU instructions use the same
opcodes and coprocessor identification (CpID) as the corresponding
instructions of the MC68851", so `PMOVE`, `PTEST` and `PFLUSH` are
F-line words — which is how the MMU registers this project already
models get written. The 68882 sits at a different ID alongside.
**The same instruction word takes different vectors depending on
privilege**, which is unusual enough to be the module's headline:
an unsupported cpID-0 word is an F-line exception (vector 11) from
supervisor state and a **privilege violation** (vector 8) from user
state. Almost everywhere else in this architecture the exception a word
takes is a property of the word alone. Reporting F-line in both cases
would let a user program distinguish "unimplemented" from "not
allowed" — exactly what the privilege violation exists to prevent.

#### **Decode dispatcher** (`src/core/cpu/m68030/ap_m68030_decode.c`) and

A word no family claims is reported **illegal rather than absorbed by a
fallback**, which is the failure mode every family module was written to
avoid.

#### **Effective address extension word counts** (`ap_m68030_ea_words`)

Table 2-3 is the part worth stating: a **byte immediate still occupies a
whole extension word** ("Low-order byte of the extension word"), so byte
and word both cost one and only long costs two. The indexed modes cost
their own extension word *plus* whatever base and outer displacements it
declares — one word for the brief format, up to five in total for the
widest full format.

#### **Total instruction length** (`ap_m68030_instruction_length`), joining

**MOVE needs two extension words, and the second is at a variable
offset.** It is the only instruction with two effective addresses, and
the source's extension words come first — so the destination's is not at
a fixed position but after however many words the source took, which the
caller cannot know until it has sized the source. Hence two parameters,
with the second documented as "the word following the source's
extensions" rather than "the second word of the instruction".
Zero means *cannot be sized* — an illegal encoding, or a coprocessor
instruction whose format varies by coprocessor and is declined rather
than guessed. Zero is never a real instruction length, so it cannot be
mistaken for one.

#### **Effective address calculation**

**A7 is not an ordinary address register.** "If the address register is
the stack pointer and the operand size is byte, the address is
incremented by two to keep the stack pointer aligned to a word
boundary", and likewise decremented. A model that misses this keeps
running — the stack merely drifts odd, and every later word or long
access to it is misaligned, which on a 68030 does not fault. The symptom
is silent corruption a long way from the cause, so it is the one rule in
the module worth knowing by heart. Tested in both directions, against
an ordinary register doing the opposite, and across the privilege
switch so it follows whichever of the three stacks A7 currently names.
The PC-relative modes are relative to the **extension word**, not the
instruction word and not the next instruction — the same base `Bcc`
uses.

#### **The logical memory access path**

**A cache hit does not consult the MMU at all**: "Whenever a read access
occurs and the required instruction word or data operand is resident in
the appropriate on-chip cache (no external bus cycle is required), the
MMU is completely ignored ... The MMU is used to validate all accesses
that require external bus cycles." That is only possible because the
68030's caches are **logically** addressed — their tag is the logical
address with the function code — so the cache can answer before anything
has been translated.
Two consequences that look like bugs and are not: a page's protection is
**not** checked on a cache hit, and the MMU's CI bit is irrelevant there,
because the MMU is not asked. A model that translates first and then
looks in the cache produces the same values with the wrong timing *and*
the wrong faults.

#### **Writes through the same path.** The asymmetry with reads *is* the

**A write to a page that has only been read costs a table search the
read did not**, which is §9.4's consequence visible end to end: an ATC
entry created by a read has M clear, and a write to it "aborts the
access and initiates a table search". Paid once — a second write to the
same page is an ordinary hit.

#### **Instruction prefetch from real memory**

**Half of all sequential prefetches are free, and which half depends on
alignment.** The holding register holds one *long word*, so a prefetch
of its odd half needs no bus cycle and no cache access. Four sequential
words therefore cost **two** fetches from a long-word-aligned start and
**three** from an odd one — no single number describes both, which is
precisely why the manual publishes an average and why
`docs/references/M68030_TIMING.md` says the published figure "is not a
value any single execution ever takes". That claim is now produced by
the memory path rather than asserted.

#### **Extension word fetching**, which unblocks every addressing mode at

The source's extension words are consumed before the destination's,
which is the ordering `ap_m68030_instruction_length`'s two parameters
exist to describe and this is where it is actually performed.

#### **The MMU instructions**, which are how every MMU register and the ATC

The encoding traps each one hides — and each was a wrong answer that
would not have faulted — are recorded in `PROJECT_STATUS.md`.

#### **Full-format indexed addressing and the memory indirect modes**. The

**So the PC no longer advances by a predicted length.** It advances by
the instruction word plus however many extension words the step actually
took, which makes the fetch and the PC agree by construction rather than
by two calculations matching. `ap_m68030_instruction_length` remains the
decoder-level answer for anything disassembling rather than executing;
the step had been calling it with zeroed extension words, which silently
means "brief format".
**The two memory indirect modes differ in where the index goes and in
nothing else** — `([bd,An,Xn],od)` against `([bd,An],Xn,od)` — so the
address calculation now reports the intermediate address *without* the
index for the postindexed mode and carries what is still owed in
`post_indirection`. Indexing in both places, or in neither, lands a
scaled register away, and for a small index that is a *nearby* address.
The indirection itself is performed in the step, not the calculation:
"The processor accesses a long word at this address", and the bus
belongs there. One resolver now sits in front of every address the step
computes, so no call site can forget it.
A **reserved** base displacement size is refused rather than treated as
null, as `ap_m68030_ea.h` always insisted.

#### **Integer ALU** (`src/core/cpu/m68030/ap_m68030_alu.c`): ADD, SUB, CMP

**Table 3-18's overbars are lost to the scan**, exactly as Table 3-19's
were — `ADD`'s overflow reads as "V = Sm Λ Dm Λ Rm V Sm Λ Dm Λ Rm",
whose two halves are identical as written and therefore unreadable. So
the formulas are *not* transcribed. They are written in the standard
equivalent form and then **verified exhaustively**: all 65536 byte
operand pairs for both add and subtract, against references computed
independently in wider arithmetic where no truncation can hide a carry.
A misplaced overbar cannot survive that, and neither can a formula that
is right everywhere except one boundary.

#### **The ALU wired into the step**: `ADD`, `SUB`, `CMP`, `AND`, `OR` and

**The direction bit decides which operand is the destination**, and with
it which way round a subtraction goes — `SUB.L D0,D1` is `D1 - D0`.
Reversing it merely negates the result, which looks almost right, and
inverts the carry in a way that only shows up in a later conditional
branch. Tested in both directions and at the borrow boundary.

#### **The immediate family and the single-operand group execute**: `ORI`

`NEG` is implemented as `0 - destination` rather than as a second
formula, because Table 3-18 gives it "V = Dm Λ Rm, C = Dm V Rm" — which
is exactly what subtracting from zero produces, so reusing the
subtraction means the two cannot drift apart. `NOT` is a *logical*
operation for condition code purposes despite looking arithmetic: V and
C cleared, X untouched.

#### **`ADDQ`, `SUBQ`, `Scc` and `DBcc` execute.**

**ADDQ to an address register is a double special case**, and both
halves are silent when missed: "the condition codes are not altered, and
the entire destination address register is used regardless of the
operation size". So `ADDQ.W #1,A0` changes all 32 bits *and* leaves the
flags alone — which is what lets a pointer be bumped inside a loop
without clobbering the comparison the loop branches on. Both halves are
tested, against a data register destination that *does* set the flags.
`Scc` writes **all ones**, not one, which is what makes its result
usable directly as a mask. `DBcc` decrements only the **low word**, so a
loop counter cannot borrow into the register's upper half.

#### **The address-register forms and the bit operations execute.**

A word A-form is **not a word operation**: the source is sign-extended
and the whole register takes part, so `ADDA.W` with a negative operand
subtracts from the full address rather than wrapping in its low half.
`ADDA` and `SUBA` alter **no** condition codes — an address calculation
must not disturb the flags a following branch depends on — while `CMPA`
does, which is the whole reason a compare against an address register is
a separate instruction.
For the bit operations the operand size comes from the **destination
kind**, not an encoding field: a data register is a *long* operation
with the bit number modulo 32, memory is a *byte* operation modulo 8. A
model picking one width addresses the wrong bit for half of all uses,
silently. And Z reflects the bit as it was **before** the operation, so
`BSET` on an already-set bit clears Z — testing after the write inverts
it.

#### **Shifts and rotates execute**, both the register form and the

**Only the arithmetic *left* shift sets V**, and it is set "if the most
significant bit is changed at **any time** during the shift" — not if
the sign differs at the end. A value whose sign shifts out and back in
sets V despite finishing as it started, which is tested directly.
**The rotates split on X**: `ROL`/`ROR` leave it alone, `ROXL`/`ROXR`
rotate *through* it. Treating all four alike breaks multi-precision
shifts, which are why the extend forms exist — verified by a nine-step
`ROXL` on a byte returning both operand and extend bit to their starting
values, against an eight-step `ROL` doing the same.

#### **CMP2/CHK2, CAS and CAS2 decode**

**The two halves count their sizes differently.** `CMP2`/`CHK2` use the
family's ordinary "00 Byte, 01 Word, 10 Long"; `CAS` uses "01 Byte,
10 Word, 11 Long", one higher throughout. The same three bits in the
same position mean a byte in one half and a word in the other, so a
decoder reading the size once for the whole escape gives every `CAS` the
wrong operand width, silently. The unassigned value moves with it: `11`
for `CMP2`/`CHK2`, `00` for `CAS`.
**And `CAS` size `00` is not merely unassigned — it is the static bit
operations.** `BSET #n,(A0)` is `$08D0`, which has the escape's shape
exactly: family `0000`, bit 8 clear, bits 7-6 reading `11`. So the two
subtrees interleave at one point, and a decoder that stops at the escape
turns every static `BTST`/`BCHG`/`BCLR`/`BSET` into an illegal
instruction — which is how this was found, three suites going red at
once. The escape declines and the decode falls through.
**`CMP2` and `CHK2` are separated by the extension word**, not the
instruction word — "identical to CHK2 except that it sets condition
codes rather than taking an exception" — so the decode reports the pair
and a second call resolves it, the same two-stage shape the indexed
addressing modes need. **`CAS2` hides behind the immediate mode's
encoding**, `111100`, which `CAS` cannot use because its operand must be
memory alterable.

#### **The immediate source, swept.** "An immediate is fetched, not

Two more were found. The **arithmetic forms' register direction** takes
an immediate source — "If the location specified is a source operand,
all addressing modes can be used" — so `ADD.W #$10,D0` in family `1101`
is a real instruction, distinct from the `ADDI` that assembles to the
same thing, and it was declining. And **`TST #<data>`** is marked
"MC68020, MC68030, MC68040, and CPU32" on its page: the 68000 had no
such form, which is exactly why a 68000-shaped model refuses it.
The remaining sites are correct by category rather than by accident: the
`100`-`110` opmodes, `MOVE`'s destination, `Scc`, the memory shifts, the
MMU instructions and `JMP`/`JSR` all take categories an immediate is not
in, and the addressing mode category module now says so.

#### **Addressing mode categories** (`src/core/cpu/m68030/ap_m68030_category.c`)

**Table 2-4 is not transcribed**, because its Alterable column does not
survive the scan — and it fails in a way that is not a plausible
reading. The extraction gives absolute addressing `—` and Program
Counter Memory Indirect `X`, which is the truth of both rows exchanged;
the same rows also give Absolute Long a register field of `000` where
`MOVEM`'s, `PMOVE`'s and `PFLUSH`'s own tables all give `001`.
So the table is **derived** from §2.3's four definitions, which survive
intact: data is everything but `An`; memory is everything but the two
register direct modes; control is memory *without an associated size*,
so not the increment modes and not the immediate; alterable is
everything but the PC-relative modes and the immediate. The derivation
agrees with every surviving cell.
Two independent checks confirm the exchanged column: `MOVE.W D0,$1234`
is a legal instruction and `MOVE`'s destination must be data alterable,
so absolute *is* alterable; and nothing PC-relative is a legal `MOVE`
destination, so the PC modes are not.
Applied where it was already being approximated: the MMU instructions
had "not a register and not an immediate", which let `(An)+`, `-(An)`
and every PC-relative mode through. `LEA` and `PEA` take control modes,
`JMP`/`JSR` take control modes, `MOVE`'s destination must be data
alterable and `NBCD`'s operand too, and `CHK` takes any data mode
including the immediate.
**The check must precede the address calculation**, which was a real
defect the first version had: the calculation applies the increment and
decrement side effects, so a refusal that came afterwards had already
moved the register. An instruction the processor refuses must leave no
trace.

#### **The original addition, implemented and backed out.**

Adding each instruction's `CC` to the bus time the core accumulated
was implemented and backed out: the tables contradict it.
`ADD Rn,Dn` is `CC 2(0/0/0)` and `NCC 2(0/1/0)` — one more instruction
bus cycle, **the same total** — so that prefetch cost nothing, having
happened while the microcode ran. `ADD Dn,EA` is `CC 3(0/0/1)` against
`NCC 4(0/1/1)`, where the extra prefetch costs *one* clock rather than
zero or two. How much of a fetch is hidden depends on how much
execution there is to hide it in, which is §11.2's "eight
independently scheduled resources ... very little of the scheduling is
directly related to instruction boundaries".
**The target is now two-sided and needs no oracle**: for any
transcribed row, a cold-cache run must come to that row's `NCC` and a
warm-cache run to its `CC`. Two published numbers bracketing the same
execution. A model satisfying both schedules the resources correctly;
one satisfying neither is adding where it should overlap.

#### **The published figures, transcribed**

The table's own markers are kept rather than flattened. The four
divides carry `+`, "Indicates Maximum Time (Actual time is data
dependent)", and are **`PROVISIONAL`**: using 56 clocks for every
`DIVS.W` would be slow by a data-dependent amount rather than wrong by
a fixed one, which is much harder to notice. Closing that needs
measurement per operand pair.

#### **The composition rule, built without the numbers**

The pairing is **directional** — the *following* instruction's head
against the *preceding* instruction's tail — and reversing it reads
plausibly, costs nothing on any single instruction, and only shows up
on a sequence whose entries have asymmetric heads and tails, which is
most of them. A test picks a pair where the two orders differ.
**Zero net execution time is a documented outcome**: "the heads of
some instructions equal the total instruction-cache-case time for
those instructions makes a zero net execution time possible". A model
clamping every instruction to at least one clock would be wrong in the
direction that *hides* a fast mode's error — it would make the
reference core slower than the hardware, so a fast mode that skipped
work would look closer to correct rather than further from it.
Head and tail compose only with CC; §11.3.3 says they do not apply to
NCC, so feeding no-cache figures through this rule would subtract a
saving the published number already excludes.

#### **The tables themselves**

**Which of the two an instruction uses is load-bearing**, not a
formality: `(An)` is `3(1/0/0)` to fetch and `2(0/0/0)` to calculate,
the difference being the operand read. Using the wrong table costs or
saves a memory access, which is the size of error C9 measured.
Two notations are carried rather than flattened. The register-direct
rows give head and tail as **`-`**, not 0 — there is no address
computation to overlap *with*, which is a different statement from an
overlap of zero. And the calculate table writes several heads as
**"2+op head"**: the head is the *operation's* head plus a figure, the
table expressing a dependency between the two halves of Equation
(11-2). Flattening that to 2 would drop whatever the operation
contributes.
The calculate table has **no immediate row**, and the lookup returns
absent rather than zero: an operand in the instruction stream has no
address to compute, so zero would read as "free" rather than as "not a
thing".

#### Two details that would produce a frame `RTE` accepts and mishandles

The word at `+$06` carries the vector **offset**, not the vector number
— TRAP #0 stacks `$2080`, not `$2020`. And formats `$3`, `$4` and `$7`
are defined by *other* M68000 family members but not by the 68030, so
accepting them would silently import another processor's frame; they are
a format error, vector 14.

#### Level 7 interrupts, which cannot be expressed as a comparison against

- Note on the priority table's wording, which reads as a contradiction and is
not: "0.0 is the highest priority, 4.2 is the lowest", then "the lower the
priority of an exception, the sooner the handler routine for that exception
executes". The higher-priority exception is *processed* first, which stacks
it deeper, so the lower-priority handler runs first and returns into it.
Reset is the stated exception to its own rule.

#### **The wider branch displacements**: `BRA`/`Bcc`/`BSR` at 16 and 32 bits

An **untaken** wide branch still consumes its displacement words — the
read has to happen before the condition is tested, or the PC lands
inside the displacement and executes it as an instruction, which decodes
as something.

#### **Closed: the TTx register bit layout, deferred and then transcribed.**

`[030]` Figure 9-37's lower half does not survive the scan — the bit
positions of E, CI, R/W, RWM, FC BASE and FC MASK OCR to nothing but a
stray "FC MASK", under both `pdftotext` modes — so the register was
modelled as decoded fields with *no* packing rather than an invented
one. The `M68000 Family Programmer's Reference Manual 1992` Figure 1-9
gives it intact: `31-24 ADDRESS BASE`, `23-16 ADDRESS MASK`, then
`E(15) 0(14-11) CI(10) R/W(9) RWM(8) 0(7) FC BASE(6-4) 0(3) FC
MASK(2-0)`, with prose agreeing field for field with §9.7.3's. That is
exactly the cross-check this item asked for, so the packing is
transcribed from two agreeing sources rather than reconstructed. The
decoded struct stays the interface every other module uses; the packing
exists because `PMOVE` moves a register image, not a struct.

#### The consistency rule, which is the part worth getting exactly right:

- Note: unlike TT, this register's bit layout *is* pinned — the prose states
"the E bit (bit 31)", the figure's column markers (31, 25, 24, 20, 16, 15,
12) survived, and the field widths are given in prose. `ap_m68030_tc.h`
records that reasoning bit by bit, so the difference from TT's deferred
packing is a documented judgement rather than an inconsistency.

#### **Descriptor semantics and accumulated search state**

Two facts that are easy to implement wrongly and that surface only once
an OS is running, so both are pinned before the walk exists:
the DT field is **context-dependent** — `$2`/`$3` describe the next
table's format in a pointer table but are *indirect descriptors* in a
page table, so a walk that ignores its level would follow an indirect
descriptor as though it were a table; and protection **accumulates**
down the tree, since "the states of all WP bits encountered during a
table search are logically ORed", making a permissive page reached
through a protected pointer still protected.

#### **Address translation cache** (`src/core/cpu/m68030/ap_m68030_atc.c`)

**An ATC hit must cost zero clocks** — "the translation time of the ATC
is always completely overlapped by other operations; thus, no
performance penalty is associated with ATC searches" — so all the time
lives in the miss, in the table search's bus cycles.

#### **MMU status register (`MMUSR`)**

and Table 9-3. One name over two different registers: the bits mean
different things depending on whether `PTEST` searched the ATC (level 0)
or the tables (levels 1-7), and a bit one form defines the other clears
outright. Two constructors rather than one with a mode flag, so neither
can produce the other's answer.
**The bit layout is transcribed, not deferred** — Figure 9-38 lost its
field boxes to the scan exactly as Figure 9-37 did, but the `M68000
Family Programmer's Reference Manual 1992` gives it intact on the
`PTEST` page (p. 6-64): `B(15) L(14) S(13) 0(12) W(11) I(10) M(9) 0(8)
0(7) T(6) 0(5) 0(4) 0(3) N(2-0)`. The two documents cross-check — the
PRM's last named single bit is T, and the 68030 manual's surviving
column markers stop at 6.

#### Separating B from I in the walk, which `MMUSR` forced and the ATC had

- "Undefined" is represented as zero, and it is a representation decision
rather than a claim: Table 9-3 marks W, S and M undefined when I is set, and
every other bit undefined when T is set. Clearing them keeps our output from
implying a guarantee the manual does not make — so an oracle diff must mask
those bits rather than treat a difference as a fault.

#### **Fill the ATC from a completed search**

A *failed* search fills an entry too, rather than leaving the address
uncached: "If a limit violation is detected, the ATC is loaded with an
entry having the bus error (B) bit set." So a faulting address does not
re-run the table search on every access — the fault itself is cached,
which is a timing claim as much as a correctness one. B folds in all
four conditions §9.4 names: bus error, invalid descriptor, supervisor
violation, limit violation.

#### **Descriptor status bit positions: derived, and labelled as derived.**

`[030]` Figures 9-10 and 9-11 did not survive the scan below the
address fields, so unlike TTx and `MMUSR` there is no second document
that simply states the 68030's. The positions are therefore *derived*,
from five sources that agree, and `ap_m68030_walk.h` records the
argument so it can be checked rather than trusted:
the `MC68851 PMMU User's Manual 3ed` §5.1.5.3 gives every position in
prose; `[030]` §9.6 says the 68030 "is program compatible with the
MC68020/MC68851 combination" and its list of MMU differences does not
include descriptor format, which it could not omit if the bits moved;
the features it *does* list as absent — access levels, gates, lockable
entries, shared-globally — are exactly the 68851 bits the 68030 has no
field for, leaving precisely the 68030's set; `[030]` Figure 9-10 does
survive in raw extraction as far as `TABLE ADDRESS` at 31-4 over a
**4-bit** status, matching the 68851's U/WP/DT being the only status
bits a table descriptor carries; and `[030]` Table 9-3 independently
says `MMUSR`'s S comes from "the S bit of a **long** format table
descriptor or long format page descriptor", confirming the 68851's
placement of S at bit 40, long format only.

#### **Cache structure and policy**

The function code is part of the tag in *both* caches, which is what
lets them survive a supervisor/user switch unflushed.

#### The data cache's write rules, which are the easiest part to get

- Note: `CACR`'s bit positions are transcribed from §6.3.1's prose ("Bit 13,
the WA bit", "Bit 9, the FD bit", …) rather than from Figure 6-14, so this
register needed no derivation. `CD`, `CED`, `CI` and `CEI` are modelled as
*actions* performed by the write rather than as fields, since all four "are
always read as zero" — storing them would invent a readable bit the hardware
does not have.

#### **The cache's half of the timing join: the `CBREQ` decision**, `[030]`

Suppressed by a clear `DBE`/`IBE`, a disabled cache, a frozen cache, or
any read-modify-write cycle.

#### The shared arbitration point itself, `board/ap_arbiter.c`: the

`arbiter_suite`, 9 tests, including the contention measurement Phase 3
asks for. Grant and acknowledgement are kept as separate instants, and
a master is never pre-empted mid-transfer.

#### **Characterised** by measurement, since no manual here lays these out:

Width, aliasing and which bits are storage are now known — the cache
control register is a *byte* mirrored across a 16-bit read, which a
transcription would have got wrong.

#### The 8259A itself, with no Apollo in it: initialization sequence, all

`i8259_suite`, 28 tests against `8259A` 231468-003. MCS-80/85 vectoring
is refused rather than approximated.

#### **Settled, and it was our assumption that was wrong.** The 8259A's

What had been imported was the AT convention that the cascade lives on
IR2. `FINDINGS.md` C11.

#### **Answered, and it is both — by level.** `008778-03` §3.2 said the

- The handler is not uniform. At **level 6 it returns the 8259's
vector**; at every other level it returns the autovector. So this
machine mixes the two, and level 6 is exactly the CPU interrupt level
this project measured separately — the two findings meet.
- What that means for us: `ap_m68030_iack_t`'s three outcomes
(autovector, device vector, spurious) are the right shape, and the
board's acknowledge must answer *device vector* for level 6 and
*autovector* for the rest, rather than choosing one policy.
- Read off the oracle's source rather than measured from a run, because
an idle boot never requests an interrupt — the same reason the level
itself needed a deliberate probe.

#### The 8237A's programming model, entire: all sixteen register addresses

`i8237_suite`, 18 tests.

#### The MC146818A itself: clock bytes, registers, RAM, the update cycle

`mc146818_suite`, 21 tests. Time comes from the caller — the oracle
seeds its calendar from the **host clock**, and copying that would make
the state hash differ on every run.

#### **Answered: only the identifier.** The oracle's `apollo_ni::call_load`

- The byte positions corroborate the placement this project measured
independently: the identifier sits at even offsets `2`, `4`, `6` — the
node ID PROM reads zero on odd bytes, which is the decode that had to
be corrected after being copied from the SIO — and the checksum is at
offset `30`, the last even byte of sixteen registers.
- The item said "a PROM with a non-zero byte elsewhere would settle it".
It did not need one: the oracle's loader states the rule directly, and
reading it cost one grep against a question that had been waiting for
media that may not exist.
- **Then verified on the real PROM we hold.** `3500_NI_1C874.bin` is 32
bytes: `00 00 01 00 C8 00 74 00` then zeros, with `3D` at offset 30.
`01 + C8 + 74 = 13D`, and `3D` is what offset 30 contains. The rule
taken from the oracle's source is confirmed against hardware data.
- Two more things fall out of the same eight bytes. The node ID reads
`01C874` from offsets 2, 4 and 6 — and the file is named
`3500_NI_1C874.bin`, so the identifier is confirmed by its own
filename. And every odd byte is zero, which is the decode this project
had to *correct* after copying the SIO's byte-pairing: the node ID
reads zero on odd bytes, and here is the PROM saying so.
- Three confirmations from one 32-byte file that has been in `roms/`
throughout: the checksum rule, the identifier's placement, and the odd
byte decode.


#### Both halves wired into the board at `04D000` and `05F800`, each

Placement characterised (`FINDINGS.md` C20): `04D000`, aliased
on an eight-byte period, offsets 1-3 driven, offsets 0 and 4-7 reading
`FF` which a read sweep cannot tell from undriven. **No manual for this part has been found yet**, and three plausible
candidates have been eliminated (`FINDINGS.md` C20): bitsavers'
`8621_AT_ESDI/` holds only ROM dumps and photographs; the *OMTI 8000
Series* reference covers models 8100/8200/8500/8600, not the 86xx; and the
*OMTI 8640* manual covers only the 8640 and describes the standard AT task
file at `1F0`, which does not match Table 2-9's `1A0` or C20's measured
three driven registers. The oracle calls the part "OMTI 8621 ESDI/floppy
controller **(Apollo)**", so it is likely an Apollo variant with its own
interface. **But the fourth candidate is the right family:**
`OMTI_AT_Controller_Series_Jan87` covers the **OMTI 8620**, "Winchesters
ESDI and ST506/412 (MFM) and Flexible Disks" — the DN3500's part in all but
its last digit, and the only candidate describing a combined ESDI/floppy
controller. §4.1 addresses the **862X**
family, not just the 8620, so the 8621 is covered outright. §4.2 gives the
fixed disk **four registers** with different meanings read and written,
"normally located at the I/O address listed in table 4-1 but may be altered
by jumpers" — which matches C20's three-driven-of-four and accounts for
Apollo's `1A0` without an Apollo-specific variant. Tables 4-1 and 4-2 are
**transcribed** in `FINDINGS.md` C21 — four ports with distinct read and
write meanings, the status register's bits, and the C/D bit that switches
the data register between 8 and 16 bits wide. The measured `C0` at the
status port is exactly Table 4-2's two "Not Used (Set to 1)" bits for an
idle controller, so manual and machine agree on a byte. Table 4-3 is transcribed too
(`FINDINGS.md` C22): the floppy half is a **conventional PC floppy
interface** at `3F2`-`3F7` or `372`-`377`, quite unlike the fixed-disk
side — one card, two programming models. Still to transcribe: Sections 5
and 6, the two command sets. And still to **measure**: where Apollo maps
the floppy half. **Found at `05F800`** — 74 KB from the
fixed-disk half, not beside it. Decodes against Table 4-3 with the block
base at AT `3F0`, and the Digital Input Register reads `80`, the
no-media bit the manual predicts. This also yielded the window's general
rule, `Apollo = 0x040000 + AT x 0x80`, fitting all three placed devices
(C22, C23).
Note §4.1's shape — floppy and fixed disk are **two independent register

#### `ORI`/`ANDI`/`EORI` to `SR` and `CCR` implemented — all six were

`step_suite`, 3 new tests including the `ANDI to CCR` case that would
otherwise drop the machine out of supervisor state.

#### **Display controller identification**, `board/ap_graphics.c` — the two

- **The blocks decode whether or not a screen is fitted.** With none, the
ID reads `FF`, matches no type, and the firmware moves on. This is the
fact whose absence sent an investigation after a phantom bug in the
exception path: "nothing is fitted" and "nothing is there" are
different answers, and only the second is a bus error.
- Each block answers only for its own family, which is how the firmware
tells which controller is present — it reads both.
- Only identification is modelled, and the header says so. Unmodelled
registers read `FF` rather than zero, because zero is a value several
of them can legitimately hold.

#### **The line 1010 and line 1111 emulator traps** (vectors 10 and 11) are

- The third test is the one that matters: an **MMU** instruction this
model has not implemented must still report `UNIMPLEMENTED`, because
the MMU is fitted and the real part would execute it. Raising F-line
there would dress our own gap up as correct hardware behaviour, and
convincingly — firmware would take a plausible exception and carry on,
and the gap would stop being visible.
- The PROM goes from 425 instructions to **2788**.

#### Main memory's *name* now stops where its address space does

`AP_BOARD_RAM_LIMIT` is `03FFFFFF`, the largest RAM a DN3500 takes —
the oracle builds its map with `DN3500_RAM_END` at `017FFFFF`,
`01FFFFFF` or `03FFFFFF` for 8, 16 or 32 Mbyte, so the base is fixed and
the *space* ends at the largest. `FFFF060E` now prints "unmapped".
- Inside the space but past the memory fitted is still *named* main
memory: the address decodes to memory, there is simply no SIMM there,
and the read refuses it. The same shape as an empty AT bus slot.
- Only the name was ever wrong; the read path bounds-checked correctly
throughout. That is why it was worth fixing rather than shrugging at —
the region enum exists to answer "what did the firmware reach for",
and a confident wrong name is what a reader acts on. `board_suite`.

#### **Found it, and my "not another absent device" call was wrong.** The

`011600` is the **master request register** and `011300` the latch-page
register: `ap_boardreg.h` has defined both since it was written, and the
map routed only the four contiguous ones at `010000-0103FF`. Two
registers existed, had their own `boardreg_suite` tests, and were
unreachable through the machine.
- That is the failure a contiguous range invites: it looks like it
covers a device and silently covers only the contiguous part.
- **The boot PROM now runs 100000 instructions with zero bus errors**,
up from 2788, and is still executing at the limit. `board_suite` has a
test that all six registers are reachable.

#### **Found it, and it retracts the "5000000 clean instructions" reading.**

At step **57** an `RTS` at `00002946` returns to `00000000`. Everything
after that is the machine walking the vector table at address 0 as
`ORI.B` instructions, four bytes at a time, forever. It never faults —
`ORI.B` on D0 over readable PROM is harmless — which is exactly why five
million instructions with zero bus errors looked like success.
**The zero-fault run was a runaway, not a boot.**

#### **Fixed, and the read path was never the problem.** `operand_write`

- "Regardless of the operand size" means the write reaches the cache
whatever its size, not that it replaces the whole entry.
- A blanket invalidate was tried first and was **too crude**: it made a
byte write hit lose the rest of the line, which `access_suite` caught.
Invalidating looks safe — it can only remove information — and is
still wrong when the manual says the data is written.
- `access_suite` was passing `true` as the *size* argument of a write.
The old whole-entry overwrite made a one-byte write of a long value
behave like a long write, so that test passed for the wrong reason for
as long as the bug existed. Corrected to `4u`. A test a bug keeps
green deserves more suspicion than one it turns red.
- The reproduction is back in `step_suite` as a passing test, and also
asserts the neighbouring bytes survive — a fix that invalidated rather
than merged would satisfy the value assertion and fail that one.

#### Characterised `FFF90000`, and **our behaviour matches the oracle**

`F8000000-FFFFFFFF` has a handler in MAME's source, `apollo_f8_r`,
returning `FFFFFFFF` without a fault — but the map line that would
install it is **commented out**, so the catch-all applies and a DN3500
bus errors there. That is what we do.
- MAME labels the region "used by fpa and/or color7?" — with the
question mark. The oracle is *itself* unsure what lives there, so the
identity is recorded as uncertain rather than asserted. What is not
uncertain is the behaviour, which is what we needed.
- So the firmware probes for a floating-point accelerator, gets a bus
error, and concludes none is fitted. Consistent with C33: this is a
probe answered correctly, not a device missing.

#### **Identified, and it is not time — it is input.** `A0` is `00010401`

- The `MOVE.L #$F0,D4` just above looks like a timeout counter, but the
`BEQ` returns to `0000078E`, *before* the instruction that loads it,
so D4 is reloaded every pass. The wait is genuinely unbounded.
- So both the firmware and our machine are behaving correctly. What is
missing is a character, and `src/frontend/headless` has no host input
by design — "deterministic: no wall clock, no host input, no threads".

#### `--boot-input TEXT`, delivering a byte sequence to SIO2 channel A as the

- Delivery **retries until the receiver accepts**. A DUART whose
receiver is still disabled drops the byte, and the firmware enables it
long after reset, so a script that advanced regardless delivered its
whole text into a switched-off port and then waited forever for the
first character. That is exactly what the first attempt did, and it
looked identical to the input never being wired up at all.
- Readiness is read through the **status register the program polls**,
not from the FIFO, so the helper cannot disagree with the machine
about whether a byte is waiting.
- With a newline delivered, the PROM leaves `000007AE` and reaches
`00000794`. Without input it still stops at `000007AE`, unchanged.

#### `ap_sio_transmit` and `--boot-console`: the machine's own console byte

- The output test exists because **a silent run is ambiguous**. It can
mean the firmware has not printed, or that the path from the
transmitter is broken, and those need opposite responses. With the
path proven, silence is evidence about the firmware.

#### **And the PROM is silent** — 300000 instructions, nothing transmitted on

Watch writes to the transmit buffers rather than guessing — the same
move that has settled every other question here.
- Answered: it is waiting for a character, not unable to print. Fed one on
the port it autobauds, the oracle prints `CR LF "MD7"` (`FINDINGS.md` C45).

#### The table also shows what the firmware has *not* touched, which is the

**Expected**, and answered by work done since rather than by a new run:
that trace ends in the console poll loop at `000007AE`, waiting for a
character. The firmware configures the interrupt controllers, probes its
buses and its display, and then *stops* to ask which console it has. It
has not begun anything that needs a clock, so a timer never read and a
calendar never touched is what a machine at that point looks like.
- The interrupt controllers being written and never read fits the same
picture: initialisation command words are write-only on the 8259A
(which is why `writetrace.lua` exists at all), so ten writes and zero
reads is a part configured and not yet serviced.
- Worth keeping as a check rather than deleting: if a later run reaches
past the console poll and *still* shows zero timer accesses, that is a
real absence rather than an early one, and this line says what changed.

#### **SIO1 is the console.** Feeding a newline to port 1 instead of port 2

The firmware is doing substantial work it was not doing before.

#### What the 11839 writes actually are: `sio1 reg 9` 4723 times

- Answered: not a counter/timer — its registers have zero *reads*. It is
the firmware cycling `CSRB` between `77` and `BB` (`FINDINGS.md` C42).

#### What it *is*: a **write-only loop**. `sio1 reg 9` 4723 writes and

- The interrupt controllers are **written 10 times and never read**, and
this machine delivers no interrupts because nothing ticks. A loop that
cannot end on anything it reads may be waiting to be interrupted.
- The tick loop is *not* established as the answer either. Reading the
code came first, and it says something else.
- Answered: it is the channel B autobaud, a rate search rather than a
wait for anything readable (`FINDINGS.md` C42).

#### **Established: the display is the console.** MAME's `dn3500()` wires the

Nothing consumes the SIO's transmit. That is why registers 3 and 11 have
zero writes: the firmware has nowhere to print *because there is no
terminal*, not because it is stuck.
- The two hints that pointed here are now one confirmed fact, and the
confirmation came from a preprocessor guard rather than from anything
the machine did. Worth noting: no amount of tracing our own run could
have found it.

#### The **graphics memories decode**: `0A0000-0BFFFF` colour and

- Both sit **inside** the AT bus memory window, so the board was
reporting the machine's own frame buffer as an empty expansion slot.
The I/O window had this hazard and a test; the memory window had the
hazard and no test, because until the memories were named there was
nothing inside it to swallow.
- No device suite could have caught it — they call the device directly
and the device was right. Only a test of the *map* sees it, and
`board_suite` has one now.
- It was not hypothetical: in the PROM run 384 accesses move from
"AT bus (empty slot)" to "display controller". The firmware was
touching its frame buffer and we were mislabelling it.

#### The graphics memories **store**, caller-owned as main memory is — this

- Three distinct ways to have nothing behind an address — no card of
that family, no memory attached, an offset past what was attached —
all read `FF`, and each has its own assertion. It would be easy to
handle the first and leave the others reading whatever the pointer
happened to be.
- The bound is **checked, not assumed from the region size**: a region
is 128 or 256 Kbyte and an attached buffer need not be, so a write
past the end would run off it.
- Storage only. A write and read back proves the memory works and says
nothing about a display; the header and the test both say so, because
a green round-trip is exactly what a working screen would also
produce.

#### `--screen c4p|c8p|19i|15i` fits a display, allocating the graphics

- The firmware behaves differently per type, which is the check that the
ID register is being read and believed: `19i` reaches `00000798`,
`c8p` reaches `000046BC`.
- With `c8p` fitted the display controller takes **803 writes**, up from
zero. The firmware initialises a display it has found, and every one
of those writes previously had nowhere to go.

#### The **control register mode fields**: `CR0` bits 7-5 select one of eight

- CR0 modes 5 and 6 and CR2 access 2 are `???` in the oracle's own
source. That is the state of the knowledge, not a gap in the
transcription, so they are named **UNKNOWN** rather than given a
plausible label — a guess would be indistinguishable from a fact until
firmware exercised it. Asserted, so it cannot be quietly filled in.

#### `CR0`'s **shift field** (bits 4-0) and `CR1`'s bits. Two gaps in the

- `CR0` carries *two* fields. Decoding only the mode left bits 4-0
reading as part of neither, and the test now pins both together — a
mode decode that forgot to shift would pick up the shift bits, and a
shift that forgot to mask would pick up the mode.
- `CR1`'s top two bits **mean different things per family**: `INV` and
`DADDR_16` on monochrome, `AD_BIT` and `DV_CK` on 4- and 8-plane
colour. Named per family rather than one set of names with a comment,
because a single name would be silently wrong on half the machines —
and wrong in the direction that still runs, since the bit would be
read, believed, and mean something else.
- Both registers are asserted to account for **every** bit, so a field
added later cannot overlap one already there.

#### Every `ap_board_t` counter now records the **first address** as well as

- It paid immediately: the empty-slot scan's first read is `00080000`,
exactly `AP_BOARD_ATBUS_MEMORY_BASE`, so the firmware's 15872 reads
are a systematic sweep of AT bus memory from its base — an expansion
ROM search, as suspected when the window was added, and now shown
rather than assumed.
- `board_suite` asserts the address for each counter, including that the
*first* rom write survives a second one.
question from what happens when it does. Check what it tests before
jumping — do not assume the jump is unconditional.
- **Answered from the counters, not a new run.** The AT bus empty-slot
reads begin at exactly `00080000`, `AP_BOARD_ATBUS_MEMORY_BASE`, and
number 15872 — a systematic sweep of the window from its base. A jump
to `00090000` is a step *within* that sweep, so it is conditional on
what the scan found there and not an unconditional branch into empty
space.
- Which also explains why it stopped mattering: once the window decoded
and read `FF`, the scan found nothing at `00090000`, the `FFFF` there
took the F-line trap, and the firmware carried on. The question was
worth asking and the answer is that the machine was already right.

#### The **special status word** and the bus fault frame layout

- The encoder applies "a rerun bit is always set when the corresponding
fault bit is set" itself, rather than trusting call sites: FB without
RB claims stage B is invalid but needs no prefetch, leaving a stale
word in the pipe on RTE.
- A rerun *without* a fault stays expressible, because that is exactly
how an address error is distinguished from a bus error.
- `ap_m68030_bus_fault_frame()` decides the frame from the SSW rather
than taking it as a parameter, because §8.2.2's "data read faults only
generate the long bus fault frame" is structural — the short frame has
no data input buffer, so a read fault stacked into it is unrepairable.
- Fields Table 8-6 labels INTERNAL REGISTER are deliberately not named.
This model has no source for the processor's microsequencer state, and
naming an offset for a field we would fill with a guess is how a guess
becomes load-bearing.

#### `ap_m68030_take_bus_fault()` — a faulting access now **takes** vector 2

- Every word of the frame is *written*, internal registers as zero.
Filling only named fields would leave whatever the stack already held
in the gaps, and a handler reading those acts on the previous
program's data. Zero is a stated value; a skipped word is not.
- The stacked PC differs by frame, and Table 8-6 is explicit: `$A` is
"at instruction boundary" and stacks the *next* instruction, `$B` is
"instruction execution in progress" and stacks the instruction that
was running.

#### **A write can now fault.** `ap_m68030_store_fn` returned `void`, so the

- `ap_m68030_access` reports the fault **before** updating the cache. A
cache holding a value external memory refused hands it back on a later
read, which is how a silently dropped write becomes a wrong *read*.
- This closes the "write side of `access_faulted` is unreachable" tail
recorded two items ago: `step_suite` now covers a faulted write taking
the **short** frame (its value is in the data output buffer, which the
short frame carries) and the refused write not surviving in the cache.

#### The boot PROM reaches **89 instructions** — an honest reading this time

The fault at `000028D0` is taken, the PROM's own handler at `00000404`
runs, and the run ends in a **double fault** when the exception stack
runs off the bottom of main memory. Bounded and deterministic.

#### **Address error (vector 3)**, which shares these frames and was defined

- "A bus cycle is not executed", so the check happens before the pipe is
touched: no prefetch is attempted and no bus error is counted. Tested
by counting fills — an implementation that let the prefetch go out
first would pass every other assertion.
- The SSW carries the rerun bits **without** the fault bits, which is
what distinguishes an address error from a bus error in the frame.
- `step_suite`, 3 tests. The PROM is unchanged at 89 with an identical
state hash, which is the check that the change is additive.

#### **A write to a read-only memory is absorbed, not refused** — a defect

- MAME's DN3500 maps the boot ROM for **write** as well as read, to a
handler that only logs — and names our exact image in a comment about
a write to address `4` from PC `2c1c`. This firmware writes to its own
boot ROM and the hardware shrugs, so faulting there would break a
program the machine runs.
- Counted as `rom_writes`, apart from `unmapped_writes`: the two mean
opposite things. An unmapped write is an address nothing answers; this
is an address something answers and cannot store.
- A *missing* PROM is still unmapped in both directions. A board whose
absent PROM refuses reads but absorbs writes describes no hardware,
and that is the hole this rule grows if it is applied by region name
rather than by what is fitted. `board_suite`, 2 tests.
- The PROM run is unchanged at 89 with an identical state hash, so its
two unmapped writes were the stack leaving RAM, not PROM writes.

#### **Resolved: there was never a handler bug** (`FINDINGS.md` C32)

`dn3500_map` maps the graphics controller registers, so a real DN3500
**answers** at `0005E801` and never faults there. The re-entry, the
stack running off the bottom of RAM and the double fault are all
artefacts of a device we have not built. Reading the PROM's handler felt
like progress and could not have reached this; one grep of the oracle's
map did.
- Recorded as a working rule: when the question is what the hardware
does, ask the oracle **before** reasoning, not after the reasoning
fails. Four findings in this chain resolved that way.

#### Close the DN4500 clock `PROVISIONAL` from a cited configuration guide

33 MHz, from `[CFG]`'s Series 4500 Product Summary; the conflicting 30 MHz
in `[CFG]`'s own overview table is recorded as a resolved discrepancy.
