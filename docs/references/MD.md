# The Mnemonic Debugger (MD) — the probe injection path

The DN3500's boot PROM "stores a read-only copy of the Mnemonic Debugger (MD)
program, and can perform diagnostics that check the fundamental operation of CPU
board hardware" — `008778-03` §1.5.1, *Boot PROM*.

This matters far more than a debugging convenience. **MD is how probes get into
the machine**, and it removes three problems at once:

- **No cross toolchain.** Instruction words are deposited as hex over the serial
  console. Nothing assembles, links, or produces an object file.
- **No executable format to reverse-engineer.** `A` and `G` need no file at all,
  so probes do not depend on first recovering Apollo's on-disk executable layout.
- **No Domain/OS boot.** MD lives in the boot PROM and runs before any OS. A
  probe measuring instruction timing does not need a booted machine, which is
  what lets probes be a *Phase 1* deliverable rather than something gated on
  Phase 4.

It also means probe runs are reproducible from a text script: a list of MD
commands is the whole input.

## Command set

From `002398-04` (*Domain Engineering Handbook* Rev 4, Jan 1987) ch. 5, *System
Debugging* → **MNEMONIC DEBUGGER (PROM)**. The handbook marks availability:
`+` = not in DNx60, `-` = DNx60 only, `*` = DN3000 only, `#` = DNx60 and DN3000
only. Unmarked commands are present everywhere.

| Command | Meaning | Availability |
| --- | --- | --- |
| `A <location>` | Access location — examine and deposit | `+` |
| `AR` | Access Control Register | `*` |
| `AS` | Display current ASID | `-` |
| `B <location>` | Breakpoint | all |
| `C <start> <end> <target>` | Copy memory | `+` |
| `CA <start>` | CALL subroutine | `+` |
| `CB <location>` | Clear breakpoints | `#` |
| `D <start> <end> <items/line>` | Dump memory | `+` |
| `DI <type><unit> <log vol>` | Define disk | `+` |
| `DL` | Down-line loader | `+` |
| `DR` | Dump registers | `-` |
| `DU` | Dump system | `*` |
| `EX <filename>` | Load and execute file | all |
| `EY <filename>` | Load and execute file, trap after load | all |
| `F <start> <end> <word>` | Fill memory | `+` |
| `FO` | Force load | `*` |
| `G <location>` | Jump to location | `+` |
| `H` | Help | all |
| `IC` | Enable/disable/show instruction cache | `#` |
| `LD` | Lists SAUn directories | `+` |
| `LO <filename>` | Load file | `+` |
| `M` | Map address space | `+` |
| `P` | Unmap address space | `+` |
| `PV` | PA-to-VA | `#` |
| `RE` | Reset system | `+` |
| `RR` | Access region registers | `-` |
| `S <start> <end> <value> <mask>` | Search memory | `+` |
| `SH <0-3>` | Spindown Winchester | `+` |
| `SK` | Select keyboard | all |
| `SS` | Single step | all |
| `TE` | Run boot PROM diagnostics | `+` |
| `V <start> <end> <target>` | Verify memory | `+` |
| `VP` | VA-to-PA | `#` |
| `XD` / `XE` | XON/XOFF disable / enable | `#` |

DNx60-only additions (`DC` data cache, `GB` halt to CPIQ, `FP` floating-point
registers) and the DNx60 micro-exec commands are recorded in the handbook but
are out of scope — the DNx60 is not a machine we emulate.

## Why each command matters to us

- `A` + `G` are the probe primitive: deposit a hand-assembled instruction
  sequence, jump to it.
- `DR` and `D` read the result back — registers and memory, which is what a
  probe's sentinel and timing counters live in.
- `SS` (single step) and `B`/`CB` (breakpoints) give instruction-granular
  control, useful when a probe disagrees with the oracle and the divergence has
  to be bisected.
- `TE` runs the boot PROM's **own** diagnostics — the hardware's test suite, for
  free, exactly as the ring firmware's self-test is for the ring controller.
- `M`/`P` (map/unmap address space) and `VP`/`PV` (VA↔PA translation) are
  directly the 68030 MMU probe surface for Phase 2, and they mean MMU
  translation can be checked against the machine's own answer rather than only
  against the oracle.
- `IC` (instruction cache enable/disable) is what makes a cache-effect timing
  probe possible: run the same sequence with the cache on and off and diff.
- `XD`/`XE` matter for harness plumbing — XON/XOFF flow control will corrupt a
  byte-exact console capture if left enabled.

## Status

`open` — the command *list* is confirmed from `002398-04`, but the exact
**syntax and output format** of each command is not yet transcribed: what `A`
prints, how it accepts a deposit, the radix, the terminator, and what `DR`'s
register dump looks like byte for byte. The oracle settles this cheaply — run
`mameapollo` with the boot PROM, drop to MD, and capture a real session — and
the harness needs the exact bytes anyway to parse them.

Until that capture exists, no probe encoder should be written against a *guessed*
MD syntax. That would be exactly the trial-and-error the project forbids.

## Sources

| Key | Document |
| --- | --- |
| `[S3K]` | *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 — §1.5.1 Boot PROM |
| `[EH4]` | *Domain Engineering Handbook* Rev 4, 002398-04, Jan 1987 — ch. 5 System Debugging, MNEMONIC DEBUGGER (PROM) |
| `[OP3xx]` | *Operating the DN3xx*, 005448-00, Sep 1985 — MD from the operator's side |
