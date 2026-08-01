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

## `H` settles it: this PROM has no `D`

Sending `H` returns:

```
ABRVPICOH
```

Nine letters, no separators.

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
