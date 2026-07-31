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

## Command grammar

`[EH4]` pp. 5-13/5-14, *Command Formats*. This was previously recorded as
undocumented, which was wrong: the handbook does not put the syntax in the
command *list*, but it continues past that list into a per-command reference
(pp. 5-7 onward) and then states the grammar formally.

```
<command>       ::= A | B | C | D | DL | F | G | S | V | <empty>
<size_spec>     ::= :I | :B | :W | :L
<base_spec>     ::= :O | :D | :H | :A
<parameter_list>::= <parameter> ...                        (up to four)
<parameter>     ::= <num_exp> | Dn | An | Rn | CCR | SR |
                    (An) | <num_exp>(An) | <num_exp>(<index_spec>) |
                    <num_exp>(An, <index_spec>)
<num_exp>       ::= <num> | * | <num_exp>+<num> | <num_exp>-<num> |
                    <num_exp>x<num>
<num>           ::= <simple_number> | $<simple_number> |
                    <base>$<simple_number> | -<num> | <quoted_string>
<base>          ::= <simple_number>
<quoted_string> ::= '<letter> ... <letter>'                (up to four)
<index_spec>    ::= An.W | Dn.W | An.L | Dn.L

a command line is:  <command> [<size_spec>] [<parameter_list>] [<base_spec>]
```

**On the transcription.** The scan's OCR is damaged in exactly the places a
grammar can least afford — `|` reads as `1`, `I` or `l`, and `::=` as `::-`.
The raw OCR of the three worst lines is `AIBICIDIDLIFIGISlvl<empty>`,
`:11 :BI :WI:L` and `:01 :DI :HI:A`, identically under both `pdftotext -layout`
and plain extraction, so the damage is in the scan and not the extraction.

The reconstruction above is nevertheless **not a guess**, because the handbook
expands every one of those tokens in prose immediately below the grammar:
`:I ::= instr-sized items, output in mnemonic format`, then `:B` byte, `:W`
word, `:L` longword; and `:O` octal, `:D` decimal, `:H` hex, `:A` ASCII. Each
alternative is independently named, so the separator is the only thing being
restored. Anything not pinned that way is left as the OCR has it. Verify
against the PDF at pp. 5-13/5-14 before relying on it for anything subtle.

### Semantics that matter for probes

- **All numeric input defaults to hexadecimal.** `$num` is explicitly hex;
  `<base>$num` sets the base, so `8$777` is octal and `2$1001` binary.
- **All addresses and offsets are *printed* in hexadecimal regardless of
  `<base_spec>`** — the output radix control applies to numbers and immediate
  constants, not to addresses.
- `<size_spec>` and `<base_spec>` may appear **anywhere** in the command line,
  and anywhere in `A` command input, except inside a quoted string.
- `*` in a `<num_exp>` is the current location. This is what makes the
  documented continue idiom `G`, then `G *+2` work.
- Unspecified parameters are set to zero; up to four may be given.
- `A [<size_spec>] <location> [<base_spec>]` — "accesses `<location>` and prints
  address and contents according to `<size_spec>` and `<base_spec>`".
- `D [<size_spec>] <start> <end> <items_per_line> [<base_spec>]` — dumps the
  bounded range, address followed by the items; default one item per line.
- `G [<location>]` — jumps after inserting breakpoints and restoring all
  registers and SR. `B` sets a breakpoint but **does not insert it until `G`**.
- `CA <start>` calls a subroutine, restoring all registers saved at the last
  entry except A0.
- `S [<size_spec>] <start> <end> <value> [<mask>] [<base_spec>]` — `<mask>`
  defaults to `$FFFFFFFF`.
- `AR` reaches the control registers by name: `TC` (MMU translation control),
  `RP` (MMU root pointer), `DFC`/`SFC` (CPU destination/source function code),
  `CACR`/`CAAR` (cache control/address). Directly the Phase 2 MMU and cache
  probe surface.

### Entering MD

With the NORMAL/SERVICE switch in **SERVICE**, `CTRL/<RETURN>` passes control to
MD at any time. AEGIS also enters MD with an `S` code after a fatal error, via a
`TRAP` from the crash routine.

## Status

`partially closed`. The command list and now the **input grammar** are
transcribed from `[EH4]`, so a probe encoder no longer has to guess how to spell
a deposit or a jump.

What remains open is the **output format**: the handbook says `A` "prints address
and contents" and `DR` dumps registers, but never shows a literal line, so the
exact column layout, separators, prompt and terminator are still unknown — and
the harness has to parse those bytes. That still wants a captured session under
the oracle, which is cheap once `mameapollo` exists.

The original rule stands for the part still open: no parser is written against a
*guessed* output format. It no longer blocks the encoder's input side.

## Sources

| Key | Document |
| --- | --- |
| `[S3K]` | *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 — §1.5.1 Boot PROM |
| `[EH4]` | *Domain Engineering Handbook* Rev 4, 002398-04, Jan 1987 — ch. 5 System Debugging, MNEMONIC DEBUGGER (PROM) |
| `[OP3xx]` | *Operating the DN3xx*, 005448-00, Sep 1985 — MD from the operator's side |
