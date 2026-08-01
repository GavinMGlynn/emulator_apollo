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
width rule, and **cannot** yet be written against the contents field. Getting
that needs a run whose input makes MD display rather than step -- and that is a
question about MD's command set, not about the harness, which now works end to
end.
