# Project status

The single source of truth for **what works**. Updated in the same commit as the
code it describes. If this file and the code disagree, the file is the bug.

**Accuracy claim: none yet, and the reason is now specific rather than
general.**

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
violations, format errors, MMU configuration errors, interrupts and trace.

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

It still never transmits: registers 3 and 11 have zero writes. The firmware
takes terminal input and does not answer on the terminal, which is consistent
with the display being its output — the second thing pointing that way, and
still not established.

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

**A machine exists, but not the DN3500.** `ap_machine` wires the 68030 to flat
RAM: construct, poke, run to a limit, read back. That is what a side-loaded
probe needs and it requires no firmware — built ahead of the boot-PROM route
because that route is in doubt (`tools/mame-oracle/FINDINGS.md` C4). There is
still no I/O and no device, so nothing boots and no end-to-end timing can be
measured.

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
report is a committed golden, checked under every build preset. This section will state exactly what backs the
claim when there is one.

Last updated: 2026-08-01 (Phase 3 boundary; subsystem table audited).

## Subsystems

| Subsystem | Status | Verification |
| --- | --- | --- |
| Build system, presets, CI | working | 4-platform matrix green on first run, plus the `-O0` vs `-O3` output-identity job |
| Model table (`model/`) | working, 9 models | `model_suite`, 13 tests |
| Time base (`time/`) | working | `time_suite`, 13 tests |
| State hash (`state/`) | primitive working | `hash_suite`, 11 tests, incl. published FNV-1a 64 vectors |
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
| 68030 instruction overlap (§11.3's Equation 11-1) | the composition rule only, deliberately without §11.6's per-instruction figures — those must be measured, not transcribed | `overlap_suite`, 8 tests, including the manual's own worked example |
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
| 68030 special status word and bus fault frame layout | working: Figure 8-9's bits, the SIZ1/SIZ0 size code that counts bytes *remaining*, FC2-FC0, and Table 8-6's field offsets for both fault frames. The encoder enforces "a rerun bit is always set when the corresponding fault bit is set", while leaving a rerun *without* a fault expressible because that is how an address error is told from a bus error. The frame is chosen **from the SSW**, not passed in: §8.2.2's "data read faults only generate the long bus fault frame" is structural, since the short frame has no data input buffer for the handler to write the faulted read's value into. Fields Table 8-6 labels INTERNAL REGISTER are deliberately unnamed — this model has no source for them. **Wired into the taker**: `ap_m68030_take_bus_fault()` builds whichever frame the SSW selects, and `RTE` returns from both | `ssw_suite`, 11 tests, `step_suite`, `[030]` §8.2.1, Figure 8-9, Table 8-6, Table 7-3 |
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
| Archive SC-499 cartridge tape controller (the part) | **register model complete**: all four addresses of `[SC499]` §1.9 — data/command, control-on-write and status-on-read, and the two write-triggered DMA commands — plus the derived interrupt flag, the tri-stated IRQ line, and RSTDMA's documented identity with power-on reset. The QIC-02 command set itself, tape motion and the drive behind it are not modelled. Not yet wired to the board at `050000` | `sc499_suite`, 9 tests, `Archive SC-499 Information Guide` |
| Apollo disk and floppy (`04D000`, `05F800`) | working: both halves of the one card, placed **74 KB apart** by measurement, each aliased through 1 KB on its own period — four registers for the fixed disk, an eight-address block for the floppy. Interrupts on IRQ14 and IRQ6, separate lines eight apart. The gap is pinned as arithmetic, not constants: the AT window maps `Apollo = 0x040000 + AT × 0x80` | `disk_suite`, 6 tests; `FINDINGS.md` C20, C22, C23 |
| OMTI command descriptor blocks | working: the 6-byte CDB decoded with the **cylinder reassembled from three bytes** (C10 in byte 1, C09/C08 in byte 2, low eight in byte 3), the command byte exposed both whole and split into class and opcode, and acceptance checked against the ESDI command set — which **refuses** `0C INITIALIZE DRIVE CHARACTERISTICS`, an ST506-only command that would make ESDI geometry look settable | `omti_cdb_suite`, 7 tests; `FINDINGS.md` C27 |
| OMTI 862X ESDI/floppy controller (the part) | **register model complete for both halves**: the fixed disk's four ports with their read/write asymmetries and the status register's fixed bits, and the floppy's five at the standard PC layout. Modelled as two independent register sets sharing nothing, as `[OMTI]` §4.1 and §3.4 describe. Both measured dumps reproduced as tests. The **command sets** (§5, §6) are not modelled — they want a drive and a disk image. Not yet wired to the board | `omti_suite`, 9 tests, `OMTI AT Controller Series Jan87` |
| OMTI 8621 placement (the DN3500's disk) | measured, both halves. Placement characterised at `04D000`: the range is the card's (all `FF` without it, control verified by device enumeration), aliased on an eight-byte period, with offsets 1-3 driven. Offsets 0 and 4-7 read `FF`, which a read sweep cannot distinguish from undriven | `FINDINGS.md` C20 |
| WD7000 ESDI/SCSI (DN4500) | not started | — |
| Floppy, QIC cartridge tape | not started | — |
| Mono and colour graphics controllers | not started | — |
| 3c505 802.3 Ethernet | not started | — |
| MAME oracle harness | working and used throughout. Beyond the dumper there are now three probe tools — `regprobe.lua` drives every bit of a register in both directions, `writetrace.lua` taps writes to watch firmware program a device, `steptime.lua` single-steps for instruction timing — and findings C10 through C14 are all measurements taken with them | `oracle_driver` (19 checks, stub MAME) and `oracle_dump_format` (19 checks, mock machine); `./apollo -listfull` lists all eleven apollo machines |
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

### Media in hand — installation tapes, and no bootable disk image

This is the constraint that shapes Phase 1's first milestone, so it is recorded
before the work rather than discovered inside it.

| Item | What it is |
| --- | --- |
| `Apollo_DOMAINOS_SR10.3.5.tgz` | 5 tapes, 176 numbered `.img` files |
| `019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct.gz` | bootable QIC install cartridge |
| `019594-001..004.CRTG_STD_SFW_{1..4}.ct.gz` | QIC install cartridges 1–4 |
| `cptape.hlp` | Apollo `cptape` help text, rev 9.0, 1986-12-17 |

The `.img` files are **tape files, not disk images** — one file per tape mark,
the layout `cptape` writes. `tape1/00.img` is 8192 bytes beginning
`00 13 d8 00 … "SYSBOOT REV" … " M68K    "` followed by 68000 code: the Apollo
tape boot record. `tape1/02.img` at 50 MB is the install payload.

**So we hold no pre-installed, bootable Domain/OS disk image — only the media to
create one.** Phase 1's first item is written as "MAME boots Domain/OS from an
SR10.x image to a login prompt", which assumes an image that does not exist.
Reaching a login prompt actually requires installing Domain/OS from tape onto a
blank disk image under the oracle first, which is a substantially larger task
than booting one. Recorded as a tail in `docs/COMPLETION_PLAN.md` rather than
absorbed silently into the item.

It also pulls `.ct` cartridge support forward in importance: Phase 4 lists it as
a storage item, but the install path makes it the format the first boot depends
on.

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

None yet. Each one added here carries its reason and cost to close.

## PROVISIONAL figures

Every entry is also a named item in `docs/COMPLETION_PLAN.md`.

| Figure | Current value | Why provisional | Cost to close |
| --- | --- | --- | --- |
| 68030 ATC replacement algorithm | first-invalid, then first entry with a clear history bit, sweeping when all are set | **Narrowed.** What the history bit *means* is no longer provisional: `MC68851 PMMU User's Manual` §5.2.1.3, describing the compatible ATC, says it indicates "that the entry has been recently used", so a translating hit now marks it. What remains unstated in both manuals is which entry is chosen *among those whose history bit is clear*. The `MC68030 Data Sheet 1991` is less specific still — "a variation of the least recently used algorithm" — and is a dead end rather than a lead | Measure eviction order against the oracle, or find a Motorola application note stating the rule; medium. Affects hit rates and therefore timing, never the translation a hit produces |
| SC-499 command handshake timings | the documented bounds | `[SC499]` §1.13.2 publishes *bounds*, not values — "0 us < T3->T4 < 150 us" says the device hands the bus back within 150 microseconds and nothing about when. Modelled at the bound, so every handshake runs at its slowest permitted speed: wrong in a knowable direction and by a knowable amount. All nine convert exactly to base units, so none is rounded on top of being provisional | Measure edge timings against a running drive, which needs the oracle's tape path exercised; small. Affects only a driver watching for the edges themselves — a polling driver cannot observe the difference |
| 68030 asynchronous input synchroniser | two clocks | `[030]` §7.7.4 publishes a bound and not a value: "all asynchronous inputs to the MC68030 are internally synchronized in a maximum of two cycles of the processor clock". The actual delay depends on where the input edge falls relative to the clock, so it is genuinely a range and one clock is as legal as two. Modelled at the documented maximum. Currently reached only by the arbitration unit's BR and BGACK, but it is the part's rule for every asynchronous input and will be shared once devices drive them | Measure grant latency against the oracle across many request phases; small once a second master exists to request the bus. Affects arbitration latency and therefore contention, never which master wins |
| MC146818A periodic interrupt, six fastest rates | not modelled | `[146818]` Table 5's rates are 32768/2^n Hz. `AP_TIME_BASE_HZ` factors as 2^9·3·5^8·11, so 1.024 kHz through 32.768 kHz are not exactly representable and `ap_clock_init` refuses them. Not an approximation — the nine slower rates are exact and implemented, and the fast six are reported unsupported rather than rounded | Recompute the time base: including 32.768 kHz costs a factor of 64 and drops the representable span from 88.6 years to 505 days. Including the part's own 4.194304 MHz crystal would cost 8192x and leave 3.95 days, so the crystal can never be a clock domain in a 64-bit base at all. Cheap to do, and deliberately not done while nothing is observed using those rates |
| DN2500 RAM base | `0x1000000` | Assumed to match the other 68030 models. The DN2500 is a single integrated board with PC-standard DRAM modules and its own memory design, and no address-space allocation table for Series 2500 has been found | Derive from the 2500 boot PROM, or find a Series 2500 hardware reference; medium |

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

- No CPU, so no machine can be constructed yet — the model table describes
  machines that cannot be built.
- The ring controller's register-level interface is not yet recovered; the
  manuals give its address window and block diagram but not its registers.
- No SDL frontend. Deliberately absent rather than stubbed.
