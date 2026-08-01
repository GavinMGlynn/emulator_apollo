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
- **The sign-on is `MD7`** with no version suffix, no banner text and no
  copyright line -- so a parser must not skip lines looking for a prompt after
  a header, because there is no header.

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

What would settle it: `H`, the help command, which the same list carries and
which is the machine's own statement of its syntax. That is one run, and it
should have been the first thing tried on a live prompt.
