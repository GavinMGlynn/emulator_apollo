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
scheduled against the bus rather than added to it as `max(microcode, bus)`,
which reproduces both the `CC` and `NCC` columns of every one of them.

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

**Phase 2 (the MC68030) is done.** Every item states a verification and every
one is met. The integer core decodes and executes the whole opcode map;
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

## Subsystems

| Subsystem | Status | Verification |
| --- | --- | --- |
| Build system, presets, CI | working | 4-platform matrix green on first run, plus the `-O0` vs `-O3` output-identity job |
| Model table (`model/`) | working, 9 models | `model_suite`, 13 tests |
| Time base (`time/`) | working | `time_suite`, 13 tests |
| State hash (`state/`) | primitive working | `hash_suite`, 11 tests, incl. published FNV-1a 64 vectors |
| Core board state hash (the identity harness's board half) | working: the board registers, the translation map, both interrupt controllers, the interval timer with its three clocks, the calendar with both cursors, both DMA controllers, both serial ports, the node ID, the disk and tape controllers, the graphics memories, the keyboard matrix and the boot PROM. The diagnostic counters are deliberately outside it and reported beside it | `board_state_suite`, 22 tests sweeping every device field by field |
| Full-machine state hash (`ap_machine_hash`, `ap_machine_state`) | working: the processor, main memory, the board when one is attached, and elapsed time — with the clock, the PC and the bus-error count reported beside the number | `machine_suite`, 28 tests, incl. the same workload run twice on two boards agreeing at every step |
| Ring medium interface | not started | — |
| Ring controller | not started | — |
| 68030 instruction pipe + cache holding register | working | `pipe_suite`, 14 tests, `MC68030 User's Manual 3ed` §11.2.2 |
| 68030 bus cycle state machine | working, including burst line fills | `bus_suite`, 23 tests, each citing `MC68030 User's Manual 3ed` ch. 7 (read, write and burst cycles) |
| 68030 bus arbitration control unit | working: the five-state machine of `[030]` §7.7.4, the processor at lowest priority, both documented deferrals (a committed bus cycle, and a locked read-modify-write) and the single-wire BGACK-alone path. Figure 7-61 did not survive the scan and the states are recovered from the prose walking it; one edge is marked `INFERRED` in code against the two passages supporting it. The input synchroniser is `PROVISIONAL` | `arb_suite`, 15 tests, `MC68030 User's Manual 3ed` §7.7 |
| 68030 on-chip instruction and data caches | working, including the bus-timing join: a hit costs 0 clocks, a burst line fill 5 | `cache_suite`, 29 tests and `bus_suite`, 23 tests, `MC68030 User's Manual 3ed` §6, §7.3.7 |
| 68030 integer ALU (results and condition codes) | working: ADD, SUB, CMP, AND, OR, EOR, NEG, NOT, and the shifts and rotates | `alu_suite`, 17 tests, `M68000 Family Programmer's Reference Manual 1992` Table 3-18; the byte space verified exhaustively |
| 68030 exception taking (stack the frame, fetch the vector through the VBR, load the PC) | working for the four- and six-word frames and the throwaway frame, wired to divide-by-zero, `TRAP #N`, `TRAPV`, `CHK`, `ILLEGAL`, privilege violations, MMU configuration errors, **interrupts** and **trace**; **the fault frames now build and return**, wired to bus error (vector 2) on any faulted access and address error (vector 3) on a prefetch from an odd program counter; reset, the coprocessor frame and the interrupt M-bit second frame decline rather than approximate | `step_suite` (10 of its tests), `exception_suite`, 16 tests, `[030]` §8.1 and Table 8-6 |
| 68030 family `0000` size-11 escape (`CMP2`/`CHK2`/`CAS`/`CAS2`) | decoded; the opcode map now has no holes. Semantics open: `CAS`/`CAS2` need an indivisible read-modify-write | `bounds_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` |
| Per-instruction timing report (`--time-instructions`) | bus and cache time only, pinned as a golden; the 0/2 alternation is the cache holding register serving two instruction words per fetch | `tests/goldens/timing.txt`; oracle side by `tools/mame-oracle/steptime.lua` |
| Probe suite (`probe/`, `--run-probes`) | 8 probes on the constructed machine, needing no firmware; results pinned as a golden under every build preset, identical between `-O0` and `-O3` | `tests/goldens/probes.txt`, `probe_suite`, 7 tests |
| Constructed machine (`machine/`) | a 68030 on flat RAM, with an out-of-range access faulting rather than wrapping; no I/O, no device, no arbitration point | `machine_suite`, 10 tests |
| 68030 published timings (§11.6) | 59 rows from §11.6.6, §11.6.8, §11.6.9, §11.6.11, §11.6.12, §11.6.15 and §11.6.16, scheduled into the step as `max(microcode, bus)` since the tables show prefetch overlaps execution. Branches are reached through their run-time outcome rather than by opcode. Seven instructions agree with the oracle (`FINDINGS.md` C8). Rows footnoted "Add Fetch Effective Address Time" are **declined**, not part-priced: their published figure is a component and the composition is open (C9). The four divides carry the manual's data-dependent marker and are `PROVISIONAL` | `timing_table_suite`, 11 tests; both published columns checked on a running machine by `machine_suite` |
| 68030 ATC replacement | the history bit now means *recently used*, per `MC68851 PMMU User's Manual` §5.2.1.3 — a translating hit marks it, a `PTEST` probe does not. `PROVISIONAL` narrowed to victim choice among clear-history entries | `atc_suite`, 20 tests |
| 68030 prefetch marginal cost | `NCC − CC` over the published prefetch count, computed in code across every row; the two rows where it is not integral are named in the test rather than rounded away | `timing_table_suite`, 11 tests |
| 68030 effective address timings (§11.6.1, §11.6.3) | fetch and calculate rows for the non-full-format modes, with the table's `-` and "2+op head" notations carried rather than flattened. Not yet composed into the step | `ea_timing_suite`, 8 tests |
| 68030 instruction overlap (§11.3's Equations 11-1 and 11-2) | both compositions, deliberately without §11.6's per-instruction figures — those must be measured, not transcribed. The cache case through head and tail, the no-cache case by plain addition, and (11-2) shown to be (11-1) over *components* rather than a second rule | `overlap_suite`, 15 tests and `ea_timing_suite`, 12 — including both of the manual's own worked examples, at 6 clocks and **40** |
| 68030 state hash (the identity harness's CPU half) | working: every architectural register, the MMU and cache control registers, the pipe, both caches, the ATC, and the accumulated clock — host pointers excluded by construction, since `ap_hash.h` has no pointer helper | `state_suite`, 12 tests sweeping every field; `step_suite`'s same-program-twice check |
| 68030 addressing mode categories (Data / Memory / Control / Alterable) | working; derived from §2.3's definitions rather than transcribed from Table 2-4, whose Alterable column is exchanged between two row pairs in the scan | `category_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §2.3 |
| 68030 operand access (read/write through an effective address) | working; a sub-long-word operand is selected from the long word by position, and one straddling two long words is split into a bus cycle per long word in address order | `operand_suite`, 13 tests, `M68000 Family Programmer's Reference Manual 1992` |
| 68030 instruction step (fetch → decode → execute → advance) | working for `NOP`, `MOVEQ`, 8-bit `BRA`/`Bcc`, `MOVE`/`MOVEA`, the six ALU operations, the `xxxI` immediate forms, `CLR`/`NEG`/`NOT`/`TST`, `ADDQ`/`SUBQ`/`Scc`/`DBcc`, `ADDA`/`SUBA`/`CMPA`, `BTST`/`BCHG`/`BCLR`/`BSET`, the shifts and rotates, `MULU`/`MULS`, `DIVU`/`DIVS`, `ADDX`/`SUBX`/`ABCD`/`SBCD` in both the register and the `-(An),-(An)` forms, `CMPM` and all three `EXG` exchanges; everything else reports unimplemented, including divide-by-zero, which needs the exception machinery | `step_suite`, 175 tests |
| 68030 instruction prefetch (pipe driven from memory) | working | `fetch_suite`, 5 tests, `MC68030 User's Manual 3ed` §11.2.2 and §6.1 |
| 68030 logical memory access path (cache → MMU → bus) | working, reads and writes | `access_suite`, 12 tests, `MC68030 User's Manual 3ed` §6.1 |
| 68030 effective address calculation (with register side effects) | working; memory-indirect modes report the pending indirection | `addr_suite`, 13 tests, `M68000 Family Programmer's Reference Manual 1992` §2.2 |
| 68030 instruction decode dispatcher (+ MOVEQ, total length) | working — 89.9% of the 16-bit space classified, and every claimed instruction sized | `decode_suite`, 17 tests including two full 65536-word sweeps |
| 68030 family 1111 (coprocessor interface, MMU instruction dispatch) | decode working — the opcode map is now complete | `coproc_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and `MC68030 User's Manual 3ed` §9.7.6 |
| 68030 family 1110 (shift/rotate/bit field) | decode working | `shift_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 arithmetic/logic families 1000, 1001, 1011, 1100, 1101 | decode working | `arith_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0000 (immediate, bit manipulation, MOVEP) | decode working; CMP2/CHK2/CAS/CAS2 not yet covered | `immediate_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 MOVE / MOVEA (families 0001, 0010, 0011) | decode working | `move_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 single-operand group (NEGX/CLR/NEG/NOT/TST/TAS, MOVE to-from SR-CCR, ILLEGAL) | working — family 0100 now complete | `single_suite`, 7 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 LEA/CHK/`$48`/`$4C` subtree (LEA, CHK, PEA, SWAP, BKPT, EXT, EXTB, NBCD, MOVEM) | working | `misc_suite`, 11 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0100 `$4E` control group (TRAP/LINK/UNLK/MOVE USP/RESET/NOP/STOP/RTE/RTD/RTS/TRAPV/RTR/JSR/JMP) | working; the rest of family 0100 not yet decoded | `control_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 |
| 68030 family 0101 (ADDQ/SUBQ/Scc/DBcc/TRAPcc) decode | working | `quick_suite`, 10 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and each instruction page |
| 68030 branch family (Bcc/BSR/BRA) decode | working | `branch_suite`, 8 tests, `M68000 Family Programmer's Reference Manual 1992` §8.2 and the Bcc/BRA/BSR pages |
| MC68030 CPU | working: the whole opcode map decodes and all but `BKPT`, `CAS`, `CAS2`, `CMP2`, `CHK2` and the non-MMU coprocessor instructions execute. Pipe, caches, bus state machine, MMU, exceptions and bus arbitration each have their own rows below | `step_suite`, 175 tests, and the per-subsystem suites |
| 68030 operation code map (top-level instruction family) | working | `opcode_suite`, 6 tests, `M68000 Family Programmer's Reference Manual 1992` Table 8-2 |
| 68030 conditional tests (the 16 Bcc/Scc/DBcc/TRAPcc conditions) | working | `cond_suite`, 9 tests, `M68000 Family Programmer's Reference Manual 1992` Table 3-19 |
| 68030 effective address decode (modes, extension words, lengths) | decode and extension-word counts working; address *calculation* needs the instruction unit | `ea_suite`, 17 tests, `M68000 Family Programmer's Reference Manual 1992` §2, Tables 2-1, 2-2, 2-4 |
| 68030 programming model (registers, SR, three stack pointers) | working | `regs_suite`, 10 tests, `MC68030 User's Manual 3ed` §1.3 and `M68000 Family Programmer's Reference Manual 1992` §1.3.2 |
| 68030 exception vectors, priority and stack frames | working; taking an exception needs the instruction unit | `exception_suite`, 14 tests, `MC68030 User's Manual 3ed` §8, Tables 8-1, 8-5, 8-6 |
| 68030 special status word and bus fault frame layout | working: Figure 8-9's bits, the SIZ1/SIZ0 size code that counts bytes *remaining*, FC2-FC0, and Table 8-6's field offsets for both fault frames. The encoder enforces "a rerun bit is always set when the corresponding fault bit is set", while leaving a rerun *without* a fault expressible because that is how an address error is told from a bus error. The frame is chosen **from the SSW**, not passed in: §8.2.2's "data read faults only generate the long bus fault frame" is structural, since the short frame has no data input buffer for the handler to write the faulted read's value into. Fields Table 8-6 labels INTERNAL REGISTER are deliberately unnamed — this model has no source for them. **Wired into the taker**: `ap_m68030_take_bus_fault()` builds whichever frame the SSW selects, and `RTE` returns from both. Two `PROVISIONAL` approximations, marked in the code: the long frame's INTERNAL REGISTER fields are stacked as **zero** because this model has no microsequencer state, and `RTE` **re-executes** the faulted instruction from the start rather than resuming mid-instruction. The second is exact when the faulted access precedes any side effect — every case the boot PROM hits — and wrong for an instruction that had already committed one | `ssw_suite`, 11 tests, `step_suite`, `[030]` §8.2.1, Figure 8-9, Table 8-6, Table 7-3 |
| 68030 ATC (22-entry, fully associative) | working; a translating hit marks the entry recently used, a `PTEST` probe does not. Replacement `PROVISIONAL` only in its victim choice | `atc_suite`, 20 tests, `MC68030 User's Manual 3ed` §9.4 |
| 68030 descriptors + search protection state | working | `desc_suite`, 23 tests, `MC68030 User's Manual 3ed` §9.5.1.1 |
| 68030 translation control (TC) + address split | working | `tc_suite`, 15 tests, `MC68030 User's Manual 3ed` §9.7.2 |
| 68030 transparent translation (TT0/TT1) | working, bit layout now transcribed | `tt_suite`, 21 tests, `MC68030 User's Manual 3ed` §9.3, §9.7.3; layout from `M68000 Family Programmer's Reference Manual 1992` Figure 1-9 |
| 68030 MMU status register (`MMUSR`) | working, both PTEST forms, bit layout transcribed | `mmusr_suite`, 16 tests, `MC68030 User's Manual 3ed` Table 9-3; layout from `M68000 Family Programmer's Reference Manual 1992` PTEST p. 6-64 |
| 68030 translation table search (the walk) | working: search, U/M writeback, and ATC fill | `walk_suite`, 40 tests, `MC68030 User's Manual 3ed` §9.2, §9.4, §9.5, §11; writeback cost cross-checked against `MC68851 PMMU User's Manual 3ed` §5.1.5.3.11 |
| 68851 PMMU and 68040 MMU | not started. The **68030's** MMU is done and has its own rows above — translation control, transparent translation, the ATC, the table walk and `MMUSR` | — |
| 68881 / 68882 / 68040 FPU | not started | — |
| Core-board registers (`010000`-`011600`) | working for the four that could be measured: CPU status (bit 15 stuck, writes clear the latched bits), CPU control and latch-page-on-parity (16 bits of storage), cache control (a *byte*, mirrored into both halves of a 16-bit read, one writable bit), each aliased across its 256-byte range. No manual here lays out these bits, so all of it is measured. **Width and storage only — no bit has a known meaning, and nothing may depend on one.** Task alias and master request are absent from the oracle and stay declined rather than modelled as all-ones | `boardreg_suite`, 12 tests; `FINDINGS.md` C10, `tools/mame-oracle/regprobe.lua`, two probe runs byte-identical |
| Address translation map (`017000`) | working: the translation itself, both DMA widths, and the register file. Between the AT bus and physical memory, not the CPU's MMU -- a DMA controller has no MMU, and this is what lets it see scattered physical pages as one contiguous run. Present on DN3500/4500/5500 and absent on DN3000, from the model table | `atmap_suite`, 15 tests, `019411-A00` §4.2.1.4, `008778-03` §1.2, §2.5 |
| Board cache (`012000` RAM, `014000` condition codes) | not started. The shared **bus arbitration point** is done and has its own row above | — |
| Apollo interrupt controllers (`011000`, `011100`) | working: the two 8259As cascaded on **IR3** (measured, not IR2 as the AT convention would have it), vector bases `A0`/`A8` from the boot PROM's own ICW2, giving levels `A0`-`AF`. Priority order matches `008778-03` Table 2-3, which with the cascade on IR3 has no anomaly. The CPU interrupt level is **6**, also measured — neither manual states it, and it took starting the interval timer by hand to make anything request at all | `intr_suite`, 12 tests; `FINDINGS.md` C11, `tools/mame-oracle/writetrace.lua` |
| Intel 8259A interrupt controller (the part) | working: ICW1-4 sequence, all three OCWs, fully nested priority with rotation, edge and level triggering, special mask and special fully nested modes, poll, AEOI, and the spurious level 7. 8086-mode vectoring only — MCS-80/85's `CALL` sequence is refused rather than approximated, and this machine never uses it. The Apollo *pairing* is a separate module | `i8259_suite`, 28 tests, each citing `8259A` 231468-003 |
| DN3500 core-board address map (`board/ap_board.c`) | working: every device placed by `008778-03` Table 2-8 and by the measurement that confirmed it, main memory at `1000000`, and an unclaimed address reported **unmapped rather than zero** — the distinction flat RAM hid, which cost 5634 invisible accesses in the first firmware run. Regions are named, so a trace can say *what* the firmware reached for | `board_suite`, 7 tests |
| Shared bus arbitration point | working: the external priority encoder `[030]` §7.7 requires, DRQ0 through DRQ7 with the processor last, driving the CPU's own arbitration unit over the three-wire protocol. A grant and its acknowledgement are separate instants, so the processor stops driving the bus when it grants rather than when the grant is taken up; a master is never pre-empted mid-transfer | `arbiter_suite`, 9 tests, `MC68030 User's Manual 3ed` §7.7, `008778-03` §2.4.6 |
| Apollo DMA controllers (`010C00`, `010D00`) | working: DMA 1 at **stride 1** and DMA 2 at **stride 2**, both measured, both aliased through their ranges. A read of a write-only register returns zero where the oracle returns `0F`; `[8237]` marks that read "Illegal", so neither is specified and ours does not invent a register value | `dma_suite`, 6 tests; `FINDINGS.md` C13 |
| Intel 8237A DMA controller (the part) | **programming model complete**: all sixteen register addresses, four channels with base and current address/count, the single shared first/last flip-flop, command/mode/request/mask/status/temporary, master clear, autoinitialise reload and the mask-on-terminal-count rule. Transfers themselves are **not** modelled and cannot be until there is a shared arbitration point to run a cycle on — a real dependency, not a scoping choice. Not yet wired to the board | `i8237_suite`, 18 tests, `8237A` 231466 |
| Apollo interval timer (`010800`) | working: the part at **odd addresses, stride 2** (measured — the region reads `00 00 00 00 00 FF ...`, the `FFFF` latch default showing through), the three §3.8 input rates as exact time-base clock domains, and the IRQ0 route. Advancing is by whole pulses, so the rate cannot become a function of how often it is polled | `timer_suite`, 8 tests; `FINDINGS.md` C12 |
| MC6840 interval timer (the part) | working for **both counting modes** — continuous and single shot, each in sixteen-bit and dual eight-bit operation — plus both control register aliases, the write/read byte buffering, the status register, the prescaler, the gate, and all five of `[6840]` §3.11's ways of clearing an interrupt. The two **measurement modes** are decoded and declined: they time a signal on a timer's gate pin, and nothing on this board drives the gates | `mc6840_suite`, 23 tests, `MC6840UM` (a scan with no text layer; read from page images) |
| Apollo calendar (`010900`) | working: **stride 1, byte consecutive** (measured — and not the timer's odd-address stride 2, so neither placement could be inferred from the other), sixty-four registers aliased through the 256-byte range, and the IRQ8 route through to vector `A8` | `calendar_suite`, 5 tests; `FINDINGS.md` C12 |
| MC146818A calendar (the part) | working: ten clock bytes, four registers, 50 RAM bytes, the once-per-second update with a full Gregorian carry, the alarm with don't-care codes, and Register C's read-to-clear. **Time is supplied by the caller, never the host** — the oracle seeds its calendar from the wall clock, which would rot every golden. The **periodic interrupt** is implemented for the nine rates that divide the time base (512 Hz to 2 Hz); the six fastest are refused rather than rounded, because `AP_TIME_BASE_HZ` factors as 2^9·3·5^8·11 and they need 2^15. Square wave and daylight-savings shifts are declined. Not yet wired to the board at `010900` | `mc146818_suite`, 21 tests, `MC146818A` (register figures read from page images) |
| Node ID PROM (`011200`) | working: the layout measured from the oracle's own PROM — stride 2 with the **odd byte reading zero** (unlike the serial ports at the same stride), the identifier big-endian in registers 0-3, and a checksum in register 14 confirmed arithmetically (`01 + 23 + 45 = 69`). The identifier is supplied by the caller, never a constant: a device whose purpose is to be unique per machine must not be the same on every one | `nodeid_suite`, 6 tests |
| Apollo serial ports (`010400`, `010500`) | working: both DUARTs at **stride 2** (measured), sixteen registers over thirty-two bytes and aliased, sharing IRQ1 through to vector `A1`. The memory-refresh square wave of §3.9 is pinned at exactly 99000 base units — its *frequency*, 66666.67 Hz, is not an integer, so a core counting in hertz could not represent this board's refresh clock at all | `sio_suite`, 6 tests; `FINDINGS.md` C14 |
| MC68681 / SCN2681 DUART (the part) | **programming model complete**: all sixteen register addresses of `[68681]` Table 4-1, both channels' mode registers with their shared pointer, clock-select, command and status registers, the three-deep receive FIFO with overrun, the interrupt status and mask registers, the input and output ports, and the counter/timer with both address-triggered commands. Serial framing itself — baud rates, start/stop bits, parity, the echo and loopback modes — is **not** modelled: a character is handed over whole. Not yet wired to the board | `mc68681_suite`, 15 tests, `MC68681 DUART Sep85` |
| QIC-02 tape drive | working for the readable half of the command set: both SELECTs with the sticky selection and the soft lock, BOT, RETENSION, SELECT Q24, READ and READ STATUS. **Writing is refused rather than discarded** — there is no write-back path, and accepting a write would let an installation appear to succeed. The cartridge *type* is supplied by the caller, because the controller derives it from tape geometry a raw image does not carry. The two opcodes the scan lost are claimed by nothing | `qic_suite`, 12 tests; `FINDINGS.md` C25 |
| Cartridge tape images (`image/ap_ct.c`) | working: block addressing over a raw `.ct` image, refusing any size that is not a whole number of 512-byte blocks, and boot-record parsing that returns the four header words. Their reading as load address and entry point is now **confirmed by the boot code itself** — its first instruction, a PC-relative `LEA`, computes word 0 exactly when executed at word 1, so the image proves its own layout. `ap_ct_boot_image` therefore *names* load address, entry point and length, and refuses a cartridge that does not announce itself, or whose header describes more than the file holds. Takes memory, never a filename, so `src/core` keeps its zero file I/O and the tests need no gitignored media | `ct_suite`, 8 tests; `FINDINGS.md` C24 |
| Apollo display controller identification (`05D800`, `05E800`) | working for **identification only**: both register blocks decode whether or not a screen is fitted, and the device ID at offset 1 reports `C4P=8`, `19I=9`, `C8P=10` or `15I=11` for the fitted family and `FF` for the other. An absent screen reads `FF` and does **not** bus error — "nothing is fitted" and "nothing is there" are different answers, and getting that wrong cost an investigation. Drawing, the blitter, the lookup table and the graphics memories are not modelled, and the header says so; unmodelled registers read `FF` rather than zero because zero is a value several of them can hold | `graphics_suite`, 6 tests; `FINDINGS.md` C31-C32 |
| Apollo cartridge tape (`050000`) | working, **controller joined to the drive**: a data-register write with the request bit set is a QIC-02 command, reads deliver the cartridge a byte at a time across the drive's block boundary, and a refused command or the end of tape raises Exception. The command handshake's **three entry conditions** are modelled — ready, exception, device-holds-the-bus, one figure each — as an *ordering*. Its timings are not: every figure in §1.13.2 publishes bounds rather than values, so modelling them means `PROVISIONAL` figures and that work is not done. The ordering is right about everything a polling driver observes. Four registers at stride 1, the upper four of each eight floating to `FF`, aliased through the range, on IRQ5 through to vector `A5`. The measured reset dump is reproduced over two aliasing periods | `tape_suite`, 6 tests; `FINDINGS.md` C16-C19 |
| Archive SC-499 cartridge tape controller (the part) | **register model complete**: all four addresses of `[SC499]` §1.9 — data/command, control-on-write and status-on-read, and the two write-triggered DMA commands — plus the derived interrupt flag, the tri-stated IRQ line, and RSTDMA's documented identity with power-on reset. The QIC-02 command set itself, tape motion and the drive behind it are not modelled. Not yet wired to the board at `050000` | `sc499_suite`, 9 tests, `Archive SC-499 Information Guide` | **Oracle note:** MAME's own SC-499 models no media change at all, so a cartridge swapped while Domain/OS holds the drive crashes it; `ext/mame` carries a local edit treating insertion as a QIC-02 RESET, per `FINDINGS.md` C56.
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
`PROVISIONAL` in the source is one of these. Audited in both directions: each
table row has a plan item, and each plan item points back here.

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
| 68882 transcendental functions | **Not implemented.** `FSIN`, `FCOS`, `FTAN`, `FATAN`, `FETOX`, `FLOGN` and the rest report unimplemented rather than approximating | §4.3.2 is explicit that there is nothing to transcribe: "the IEEE specification does not define the error bound to which transcendental (**except square root**) functions are to be performed", and Motorola publishes no algorithm -- only bounds. "The worst-case accuracy of any transcendental function is one unit in the last place of double precision (which is equal to 4096 units in the last place of extended precision). The typical error bound ... is approximately 64 units in the last place of extended precision." So a correctly-rounded implementation would be *more* accurate than the part and would differ from it in the low bits of almost every result -- which for a reference core is a divergence, not an improvement. Reporting unimplemented keeps that visible; approximating would hide it | Either recover the algorithms from a 68881 ROM disassembly, or accept a correctly-rounded implementation as a **named divergence class** with the published bound as its size. The second is cheap and the first is the only one that reproduces the hardware's bits. Affects only the transcendentals; every IEEE-specified operation, square root included, is exact |
| SC-499 command handshake timings | the documented bounds | `[SC499]` §1.13.2 publishes *bounds*, not values — "0 us < T3->T4 < 150 us" says the device hands the bus back within 150 microseconds and nothing about when. Modelled at the bound, so every handshake runs at its slowest permitted speed: wrong in a knowable direction and by a knowable amount. All nine convert exactly to base units, so none is rounded on top of being provisional | Measure edge timings against a running drive, which needs the oracle's tape path exercised; small. Affects only a driver watching for the edges themselves — a polling driver cannot observe the difference |
| 68030 asynchronous input synchroniser | two clocks | `[030]` §7.7.4 publishes a bound and not a value: "all asynchronous inputs to the MC68030 are internally synchronized in a maximum of two cycles of the processor clock". The actual delay depends on where the input edge falls relative to the clock, so it is genuinely a range and one clock is as legal as two. Modelled at the documented maximum. Currently reached only by the arbitration unit's BR and BGACK, but it is the part's rule for every asynchronous input and will be shared once devices drive them | Measure grant latency against the oracle across many request phases; small once a second master exists to request the bus. Affects arbitration latency and therefore contention, never which master wins |
| MC146818A periodic interrupt, six fastest rates | not modelled | `[146818]` Table 5's rates are 32768/2^n Hz. `AP_TIME_BASE_HZ` factors as 2^9·3·5^8·11, so 1.024 kHz through 32.768 kHz are not exactly representable and `ap_clock_init` refuses them. Not an approximation — the nine slower rates are exact and implemented, and the fast six are reported unsupported rather than rounded | Recompute the time base: including 32.768 kHz costs a factor of 64 and drops the representable span from 88.6 years to 505 days. Including the part's own 4.194304 MHz crystal would cost 8192x and leave 3.95 days, so the crystal can never be a clock domain in a 64-bit base at all. Cheap to do, and deliberately not done while nothing is observed using those rates |
| 68030 long bus fault frame's internal registers | Stacked as **zero**. This model has no microsequencer state to save, so the fields Table 8-6 labels INTERNAL REGISTER are written rather than skipped — a stated value, where a skipped word would leave whatever the stack already held | `[030]` Table 8-6 | An `RTE` resuming a fault *mid-instruction* cannot work from a zeroed frame |
| 68030 full-format effective address rows: which of §11.6.1's two groups an encoding selects | A **word** base displacement is free when the base is a register and costs 2 clocks when it is suppressed; a long one is never free | §11.6.1 publishes its full-format rows in two groups, one written `d16,An` and one written `B` — and its own footnote defines `B` as "0, An, PC, Xn, An + Xn, PC + Xn. Form does not affect timing", which makes the groups overlap and contradict: `(d16,An)` is 6 clocks and `(d16,B)` is 8. The reading is what makes the table consistent — **every** group A row equals its group B row with the base displacement dropped, all eight with no counterexample — and the head column agrees independently, 2 for the free rows against 4. `MC68020 User's Manual` §9.2.1 corroborates from outside: the same table with the same footnotes and *no* `d16,An` group at all, its `(d16,An)` costing 2 more than its `(B)`, so the free displacement is a 68030 addition. Strong, and still a reading | Three readings through `steptime.lua`: a full-format `(d16,An)` with the index suppressed, the same with it present, and a null base displacement as a control both readings agree on — so a disagreement there means the transcription is wrong rather than the mapping. Small, and the harness exists. Affects every full-format effective address by up to 2 clocks, never the address it computes |
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
- No DMA transfers, and no shared arbitration point for them to contend at.
- The ring controller's register-level interface is not yet recovered; the
  manuals give its address window and block diagram but not its registers.

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
