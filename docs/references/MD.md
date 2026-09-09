# MD, the Apollo Mnemonic Debugger: captured output format

Phase 1 required this before the harness that parses MD could be written. The
handbook says `A` "prints address and contents" and never shows a literal line,
so the column layout, separators, prompt and terminator were unknown -- and a
parser must match exactly those bytes. Guessing them was ruled out; this is a
capture.

## How it was captured

`tools/mame-oracle/mdcapture.lua` against `dn3500`, with:

- the oracle **rebuilt with `APOLLO_XXL`**, which compiles in
  `apollo_stdio_device` -- the only thing wired to serial 1 channel B's
  receiver, and channel B is the port MD talks on;
- service mode set through the `:apollo_config` port;
- a key press on the Apollo keyboard to prompt the firmware's autobaud;
- **one carriage return every 0.4 s on standard input**, not a pipe delivered at
  once. This is the part that matters: `apollo_stdio_device::poll_timer` drains
  a ready pipe in a single callback and then sees EOF, so a burst arrives long
  before the autobaud runs and is discarded. The probe needs a signal *during*
  the probe.

Reproduce with:

```sh
(for i in $(seq 1 120); do printf '\r'; sleep 0.4; done) | \
  APOLLO_MD_UNTIL=45 APOLLO_MD_POST="Numpad Enter" \
  python3 tools/mame-oracle/oracle.py run --machine dn3500 --at 45 \
    -- -autoboot_script "$PWD/tools/mame-oracle/mdcapture.lua"
```

## The bytes

Every byte MD writes to serial 1 channel B's transmit buffer, in order. Sign-on
first:

```
0D 0A 4D 44 37 0D 0A
```

which is:

```
CR LF 'M' 'D' '7' CR LF
```

Then, for each carriage return received, exactly this and nothing else:

```
0D 0A 0D 0A 3E
```

which is:

```
CR LF CR LF '>'
```

## What that settles

- **The prompt is a single `>`**, `0x3E`, with no trailing space and no
  preceding text.
- **The line terminator is `CR LF`**, `0D 0A`, in that order -- not `LF` alone
  and not `LF CR`.
- **A blank line precedes each prompt.** The `CR LF CR LF` before `>` is two
  terminators, not one: MD ends the previous line and then emits an empty one.
  A parser expecting a single terminator will read the blank line as a response.
- ~~**The sign-on is `MD7`** with no version suffix, no banner text and no
  copyright line -- so a parser must not skip lines looking for a prompt after
  a header, because there is no header.~~ **Wrong, and struck rather than
  deleted** -- see "The sign-on is longer than this capture saw" below. The
  capture above stopped mid-line and this read the stop as the end of the line.

## The sign-on is longer than this capture saw

Recaptured through `tools/mame-oracle/mdsession.py`, which holds the session
open on a pty instead of running to a fixed emulated second. The sign-on is:

```
0A 4D 44 37 43 20 52 45 56 20 38 2E 30 30 2C 20
31 39 38 39 2F 30 38 2F 31 36 2E 31 37 3A 32 33
3A 35 32 0A
```

which is:

```
LF "MD7C REV 8.00, 1989/08/16.17:23:52" LF
```

There *is* a header, and it carries a revision and a build date.

The `CR`s are absent from this *stream*, not from the line:
`apollo_stdio_device::rcv_complete` drops `\r` on its way to stdout. The leading
one is confirmed independently -- the register tap above caught `0D 0A` before
the `M` -- and the trailing one is inferred from that same pattern rather than
observed here. So the register tap remains the record of what the DUART carried,
and this is the record of what the line says.

### Why the first capture was wrong, which is the part worth keeping

The bytes in "The bytes" are not misread. `0D 0A 4D 44 37` is genuinely the
start of this same line -- `CR LF M D 7` -- and `MD7C` continues from exactly
there. What the capture did not have was the *rest*, because
`APOLLO_MD_UNTIL=45` stopped the machine partway through the banner, and
`FINDINGS.md` C45 says so in as many words: "What remains is mechanical: run
long enough to get a full prompt and a command response."

The trailing `0D 0A` that made the line look finished is the truncation, not a
terminator. So a capture that ends inside a line is indistinguishable from one
that ends at the end of a line -- unless something independent says which, and
here nothing did. The conclusion drawn from it went further than the bytes did:
"no version suffix, no banner text and no copyright line" is a claim about
bytes that were never observed.

The general form, since it will happen again: **a bounded capture proves what it
contains and nothing about what follows.** The bound has to be lifted, or the
absence has to be shown some other way, before an absence can be reported.

## The `A` command's line

Sending `A`, then an address, then repeated carriage returns produces a run of
lines. Byte-exact, consecutive:

```
0D 0A 34 3A 20        CR LF '4'  ':' ' '
0D 0A 36 3A 20        CR LF '6'  ':' ' '
0D 0A 38 3A 20        CR LF '8'  ':' ' '
0D 0A 41 3A 20        CR LF 'A'  ':' ' '
...
0D 0A 31 30 3A        CR LF '1' '0' ':'
0D 0A 31 32 3A        CR LF '1' '2' ':'
```

What that gives, and only what it gives:

- **The separator is `':' ' '`** -- colon then a single space -- for
  single-digit addresses. For two-digit addresses the trailing space is
  **absent**: `31 30 3A` is `10:` with `CR LF` next. So the field is
  space-padded to a fixed width rather than colon-then-always-space, and a
  parser splitting on `": "` will fail from address `10` onward.
- **Addresses are bare hexadecimal, upper case, without leading zeros** --
  `4`, `6`, `8`, `A`, `C`, `E`, `10`, `1A`, `2E`.
- **The step is 2**, so `A` walks words rather than bytes.
- Each line begins with `CR LF`, consistent with the prompt.

## What is still not captured

**The contents.** Every line above ends after the address field -- MD is
prompting for input at each address and our carriage returns simply advance it,
so nothing was ever displayed to the right of the separator. The handbook's
"prints address and contents" describes a case this capture did not reach.

So the parser can be written against the address field and the separator's
width rule, and **cannot** yet be written against the contents field.

## MD's disk error line, and where its number comes from

`002398-04` p. 12-11 gives the DN3000's disk sense codes and then says what MD
does with them:

> Error codes 'xx' are returned by `sense_status` and reported by MD driver as:
>
> ```
> DISK operation ERROR: xx recordnum unit type
> ```

Four fields after the code, in that order. `xx` is the controller's sense byte
from `03 REQUEST SENSE`, so a capture showing this line names the failure exactly
— `21` an address the drive does not have, `23` a multiblock transfer that ran
off the end, `17` a write-protected drive, `04` no drive. The full list is on the
same page and is transcribed in `ap_omti.c` beside the codes this core emits.

Worth having before a floppy or Winchester capture rather than after: the line is
the only place MD reports *why* a transfer failed, and without the format it
reads as four unlabelled numbers.

## The banner names the SAU, and the DS5500's is `MD14`

A DN3500 prints `MD7C REV ...` and a DS5500 prints

    MD14 REV 2.00, 1991/03/08.16:20:14

— reached 2026-09-10, `PROJECT_STATUS.md`. The digits are the **SAU number**,
`008860-A03` Table 1-1's, the same 7 and 14 that name `/sau7` and `/sau14`, so a
capture's first line says which machine's monitor is talking before anything
else does. Worth having when two consoles are open, and worth having in a script
that waits for a prompt: `tools/md-shell.script`'s `expect MD7C` is a DN3500
expectation and will not match a DS5500.

## MD's *boot* error line, which is a different line with a different table

`002398-04` p. 4-17, "BOOT ERRORS (PROM)" — read as a page image 2026-09-09 and
**not the same line as the sense-code one above**, which is a running driver's
report of a failed transfer. This one is the boot path's:

```
error: boot not found - The SYSBOOT read from records 2 thru B did not have a
                        good boot header.

disk init error  <SC> <RCD> <UNIT> <W/F/S/C>
disk read error  <SC> <RCD> <UNIT> <W/F/S/C>

    SC   = Status Code
    RCD  = Record Address
    Unit = Disk Unit No.
    W/F/S/C = Winchester/Floppy/SMD/Cartridge Tape
```

### "records 2 thru B" is ten *records*, and a record is a page

Added 2026-09-10 (`FINDINGS.md` C276), because the page's phrase is exact and its
unit is not the sector. The boot PROM reads ten records starting at record 2 and
places each one a **page** further into its buffer, and the page size is chosen
by the machine's SAU number — `5500_BOOT_A1631-80046_1-30-92.bin` at `0018BE`:

| SAU number in `$174(a6)` | buffer stride | records read |
| --- | --- | --- |
| `5` | `$200` | — |
| `$B` (11) | `$800` | 5 |
| anything else, **14 included** | `$1000` | 10 |

So on a DS5500 "records 2 thru B" is sectors **8 through 47**, four 1056-byte
sectors to a record, where the DN3500 PROM reads the same ten records as ten
single sectors, 2..11. **Both are "records 2 thru B" and they are disjoint after
sector 11.**

The *format* of a record is the same on both, and a first reading of this said
it was not: what reaches the record buffer is the sector's payload, past the
32-byte Domain block header — measured, `010FAFE0`–`010FAFFF` is still unwritten
after the read — so the good boot header the page speaks of is at payload `+$00`
and the signature at payload `+$10` either way.

The header must also carry a processor tag eight bytes past the signature, and
on a machine that is not SAU 5 or SAU 11 the PROM requires **` M68K_4K `** where
a DN3500 volume carries ` M68K    `.

### The DS5500 PROM's wording, and a second message the page does not list

`002398-04` p. 4-17 prints the message as `error: boot not found`. The DS5500's
PROM spells it **`error: sysboot not found`** (string at `0196C`), and carries a
second one the page has no row for:

```
error: incorrect sysboot installed
```

at `01988`, reached when the signature matched but the processor tag did not.
The distinction is worth having before a capture: *not found* is the boot area
in the wrong place or the wrong shape, *incorrect* is the right area with the
wrong SYSBOOT in it.

**The observed lines carry the same four fields under a different leader.** This
core's runs print `Disk C8  FFFCFF  00  W` and `Tape C0  000000  00  C`, so the
qualifier letter and the field order are confirmed even though the leader is not
the page's.

Three status-code tables follow. Winchester's five (`1` not responding, `2` not
ready, `11` seek not complete, `12` CRC/timeout/buserr/overrun, `13` drive
faults) and the floppy's twelve are on the left and right; the **Disk/Tape**
table is the one a tape capture needs:

| | | | |
| --- | --- | --- | --- |
| `11` controller diagnostic failed | `1E` disk still busy | `21` seek did not complete | `26` drive not ready |
| `12` controller timed out | `1F` controller still busy | `22` write fault | `27` no track 0 on restore |
| `13` illegal controller command | | `23` unit not present | `28` address mark not found |
| `15` memory parity during dma | | `24` sector not found | `29` ECC error in sector ID field |
| `16` dma overrun/underrun | | `25` no index pulse | |
| `17` dma not at end of range | | | |

and the tape's own range, which is `QIC-02 Rev D` §5.3 a third time (`RING.md`
records the same rows reached from the standard and from Domain/OS module `28`):

`30` illegal tape command, `31` filemark encountered, `32` and `33` read error —
no data and BOM *(both rows read the same; §5.3 makes one of them EOM and the
page does not say which)*, `34` no data, `35` filler block transfer, `36` bad
block transferred, `37` read or write abort, `38` end of media, `39` drive not
present, `3A` no cartridge in drive. Then one on its own: **`FF` timeout waiting
for controller done**.

**What the table does *not* contain is the code this core provokes.** `C0` on
the tape path and `C8` on the disk path are outside every range above, so either
these PROMs' code set is wider than the Rev 4 handbook's or the two high bits
carry something it does not describe. Recorded here so the next reader does not
spend the search twice: **a status code above `FF`'s row is not decodable from
anything held**, and the way into such a failure is the register traffic.

## `A` is not the display command

From the Engineering Handbook (`002398-04`, "MNEMONIC DEBUGGER (PROM)"), the
command list resolves this rather than experiment:

```
A <location>                  Access location
D <start> <end> <items/line>  Dump Memory
```

`A` *accesses* -- an examine and alter loop, which is exactly the address-then-
prompt behaviour captured above. **`D` is the display command**, and its output
is the format the parser actually needs.

## MD echoes its input

Sending `D 1000 1020` one character at a time, 0.3 s apart, brings back:

```
31 30 30 30 31 30 32 0D      '1' '0' '0' '0' '1' '0' '2' CR
```

Two things follow.

- **MD echoes received characters.** A harness reading this stream sees its own
  input interleaved with MD's output and must account for it; a parser that
  assumes everything arriving is a response will mis-read every command it
  sends.
- **The echo is selective, and it is not rate loss.** Sending
  `D ␣ 1 0 0 0 ␣ 1 0 2 0` echoes `1 0 0 0 1 0 2` at both 0.3 s and 0.9 s
  spacing -- the *same* characters absent at both rates. A dropped-character
  problem would vary with pacing; this does not. What is missing is the command
  letter, both spaces, and the trailing digit, which is the shape of MD echoing
  parsed *arguments* rather than raw input.

  So the earlier reading of this as rate-sensitive loss was wrong. Slowing the
  input threefold changed nothing, which is the measurement that distinguishes
  the two and which should have been made before concluding the first time.

`D`'s output format is therefore **not yet captured**: no dump follows the
command, only the usual prompt. Since the digits do arrive and the pacing does
not matter, the obstacle is **not** delivery, and the next question is what MD
does with a command it has received -- whether the syntax is wrong, whether the
address range is rejected, or whether the echo is of a line it never executed.

The remaining unknowns are now about MD rather than about the harness, which is
a better place to be stuck than the previous one.

The handbook does not settle it either. It gives the syntax line
`D <start> <end> <items/line>` and **no example**, so argument separators,
radix and whether the third argument is optional are all unstated -- the same
gap that made this whole document necessary for the `A` line. `D` is marked `+`,
"not in DNx60", so it is present on a DN3500 and the command is not the problem.

## `H` returns a command table, and the nine letters were a fragment

**This section's original conclusion was wrong, and the correction is worth more
than the conclusion was.** It recorded `H` as returning

```
ABRVPICOH
```

nine letters with no separators, *inferred* that they were a command list, and
concluded from `D`'s absence that this PROM has no display command. Every part
of that is mistaken.

Run against `apollo-headless`, with the autobaud given enough carriage returns
to finish first, `H` returns a **formatted table**:

```
A   ACCESS MEM    B   BRKPOINTS     C   COPY MEM      CA  CALL
CB  CLR BKPTS     D   DSPLAY MEM    DL  DOWN LOAD     DP  DUMP OS
DR  DUMP REGS     DU  DUMP SYSTM    F   FILL MEM      G   GO
PV  PA-TO-VA      S   SEARCH MEM    SS  SNGLE STEP    V   VERIFY MEM
VP  VA-TO-PA      XE  ENABLE XON    XD  DISABL XON    AR  ACC CTRLRG
DI  DEFINE DSK    EX  EX (CPU)      EY  EX-N-TRAP     FO  FORCE LOAD
IC  INST CACHE    DC  DATA CACHE    LD  LIST SAU      LN  LIST NETS
LO  LOAD FILE     M   MAP MODE      P   PHYS MODE     RE  RESET
SH  SHUT DISK     SK  SEL KEYBD     TE  TEST          H   HELP
```

Thirty-six commands in four columns. `D` is there -- `DSPLAY MEM` -- and so are
`EX`, `EY`, `LO`, `FO` and `DL`, every one of which this document had concluded
was absent from the image.

**The nine letters are a fragment**, and their shape says which: `A`, `B`, `R`,
`V`, `P`, `I`, `C`, `O`, `H` are single characters that appear in the table's
text. A capture that stopped early, or one that sampled a partly-transmitted
line, would produce exactly that -- letters in no order, with no separators,
looking enough like a list to be read as one.

### What was actually wrong with the earlier reading

Not the bytes. The *inference*, and it is the same shape of mistake twice over.
The section reasoned that "a help command whose output is a run of letters, two
of which are known commands, is a command list", marked that as inference, and
then used it as a premise: `D` is not in the string, therefore `D` does not
exist, therefore `D` producing nothing is explained. Each step is reasonable and
the chain is wrong, because the first step's evidence was incomplete in a way
nothing in the string itself could show.

The corrective was available and not taken: `002398-04` §5 lists `D` for this
machine family, and this document set that aside on the grounds that the
handbook's markers "do not distinguish PROM revisions". They do not -- but a
document disagreeing with a measurement is a reason to re-measure, not to
discard the document.

`D` produced nothing for some other reason, which is now an open question again
rather than a closed one. The likeliest is the one that hid the contents field
for just as long: `D` with no separator is a syntax error, exactly as `A1000`
was, and the capture that "showed `D` does not exist" was sending `D 1000 1020`
through a path that ate the spaces.

**It is a command list, and this is inference from three observations rather
than a fourth run.** Two of the nine are already confirmed to work: `H` returns
this very string, and `A` walks memory. A help command whose output is a run of
letters, two of which are known commands, is a command list. `R` returning `E`
is then a command rejecting bare invocation, not evidence against the list --
`A1000` returns the same `E`, and `A` unquestionably exists.

Marked as inference because it is one: the alternative, that the string means
something else and `H` and `A` coincidentally appear in it, is not excluded by
measurement. It is excluded by there being no other reading in which a help
command emits its own letter and the memory-examine letter and seven more.

What is measured, and independent of all that: `D` does not appear in the
string, and `D` produces nothing.

So `D` produced nothing because **the command does not exist here**. The
handbook's list at `002398-04` describes a fuller MD than this image carries;
its per-command markers distinguish machine *families* (`+` not in DNx60, `•`
DN3000 only) and do not distinguish PROM *revisions*, so a command marked
available for DN3500 may still be absent from a particular DN3500 image. This
one is `3500_BOOT_12191_7`.

That is worth more than the answer it gave. A document listing a machine's
commands was wrong about this machine, in the direction that costs the most --
it named something that is not there, so every attempt to use it looked like a
syntax problem rather than an absence.

**`A` is the only memory-examining command confirmed to work on this PROM** --
confirmed by use rather than by the help string -- which makes the
address-and-prompt format captured above the format the parser must read, not a
stepping-stone to a nicer one. The contents field appears when `A`
is given input that displays rather than advances, and the command set is now
small enough to establish that from the machine itself.


## The address-and-contents line has a second, better source

`002398-04` §4, "MNEMONIC DEBUGGER ERROR CODES (PROM)", lists what MD prints on
entry after a crash. Every entry has the same two-line shape:

```
A   <PC> <SR> <IR> <FA> <FC>   -  Address Error
    <PC> <Contents>
B   <PC> <SR> <IR> <FA> <FC>   -  Bus Error
    <PC> <Contents>
U   <PC> <SR>                  -  Unimp inst trap
    <PC> <Contents>
```

**`<PC> <Contents>` is the address-and-contents line.** It is printed on every
crash entry, without any command being typed -- so the format the parser needs
can be captured by *causing a fault* rather than by finding the right arguments
to `A`. That is the route this document should take next, and it is also the
case the harness most needs to read: crash analysis is what MD is for.

The table also settles the `E` from `A1000`. The crash codes are
`A B C F I J o T U V W X Y` -- **there is no `E`** -- so `E` is a
command-syntax response and not a crash code, and looking for it in this table
was the wrong table. Recorded because the lookup was still worth doing: it cost
nothing, and it produced a better route than the one it was meant to unblock.


## The contents field, captured at last -- and `A` takes a **space**

Both open questions above are closed, and the second one explains why the first
stood so long.

`A1000` is a *syntax error*. `A 1000` -- with a space -- is the command:

```
>A1000␍␍␊E␍␊>
>A 1000␍␍␊1000:  150 ␍␍␊1002: 2D5F ␍␍␊1004:  154 ␍␍␊1006: 4E7A ␍␍␊...
```

So `E` was never MD rejecting an address or a bare invocation. It was MD
rejecting a command with no separator, and every earlier attempt to reach the
contents field was made with the form that cannot work.

The line, byte-exact:

```
0D 0D 0A 31 30 30 30 3A 20 20 31 35 30 20    CR CR LF '1000' ':' ' ' ' ' '150' ' '
0D 0D 0A 31 30 30 32 3A 20 32 44 35 46 20    CR CR LF '1002' ':' ' ' '2D5F'    ' '
```

- The value is **right-justified in a four-character field** with leading zeros
  suppressed, so `0150` prints as `␣150` and `2D5F` fills it. With the
  separator's own space before it the short case reads `:␣␣150` and the full one
  `:␣2D5F`, which is the same padding rule the address field has and the same
  trap: a parser splitting on `": "` gets a different number of fields depending
  on the value.
- One trailing space after the value.
- Each line begins `CR CR LF`: the echo of the carriage return that advanced it,
  then MD's own `CR LF`.
- The step is 2, so `A` walks words, matching the address-only capture.

**And the values are right.** Against `3500_BOOT_12191_7.bin` read directly:

```
1000: 0150   1002: 2D5F   1004: 0154   1006: 4E7A   1008: A801   100A: B5FC
```

All six, including both leading-zero cases. MD is reading memory through this
core and reporting it correctly, which is a stronger statement than the format
being parseable.

**MD echoes the whole command line here** -- `A 1000` comes back entire, spaces
included. That refines the "echo is selective" reading above rather than
contradicting it: what that capture saw was `D`, a command this PROM does not
have, so the echo it produced was of a line MD never accepted. A command MD
*does* accept is echoed as typed.

The crash-entry route recorded above is no longer needed for the format, though
it remains the right way to reach the crash codes themselves.

## This core produces the same stream

Booting `3500_BOOT_12191_7` under `apollo-headless` with carriage returns paced
onto serial 1 channel B:

```
0D 0A 4D 44 37 43 20 52 45 56 20 38 2E 30 30 2C 20
31 39 38 39 2F 30 38 2F 31 36 2E 31 37 3A 32 33 3A
35 32 0D 0A 3E
```

which is `CR LF "MD7C REV 8.00, 1989/08/16.17:23:52" CR LF '>'`.

Against the oracle's, character for character:

| | sign-on |
| --- | --- |
| oracle, via `mdsession.py` | `0A` … `0A` with the text between |
| this core | `0D 0A` … `0D 0A` with the same text |

The difference is the `CR`s, and this file already accounts for them: MAME's
`apollo_stdio_device::rcv_complete` drops `\r` on its way to stdout, and the
leading one was confirmed independently by the register tap that caught `0D 0A`
before the `M`. **What this core emits is what that tap saw** -- the line as the
DUART carries it, with nothing removed.

The prompt agrees as well. This file records `CR LF CR LF '>'`; this core emits
`0D 0A 0D 0A 3E`, preceded by the `0D` echo of the carriage return that prompted
it, which is the echo behaviour recorded below.

### What it took, and why the stream was empty before

Seven defects in this core, each of which had been quietly making every earlier
firmware run meaningless, and none of them in the serial code:

1. the boot loop stepped the processor rather than the machine, so **no time
   passed** -- `elapsed 0 base units` on every run this project had taken;
2. `--boot-key` never delivered a byte, because `MR1` resets to a five-bit link
   and a disabled receiver drops what arrives;
3. scripted input had the same defect;
4. the 68681's counter reached terminal count one clock late;
5. a rate mismatch set a flag and delivered the byte **intact**, where a UART
   returns a different value because it sampled at the wrong instants -- and the
   firmware's autobaud identifies the rate *from* that value;
6. device registers were **cacheable**, so a polled status bit was read once and
   then forever out of the cache;
7. a byte read ran a **long-word** bus cycle, popping the receive FIFO twice and
   handing the program the second pop.

And then the pacing this file had already prescribed. The last character could
not be delivered faster than the wire carries it -- ten bit times at the line's
own rate -- and sending as soon as the FIFO emptied put the byte that should
have arrived *after* the firmware rewrote its clock select in front of it
instead, consuming the armed state and leaving the clean character with nothing
to take it. "One carriage return every 0.4 s, not a pipe delivered at once" was
recording a requirement, not an incidental of how the capture was driven.
