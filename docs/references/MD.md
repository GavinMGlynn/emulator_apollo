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

## What is not yet captured

Command *responses*. The session above only ever sends carriage returns, so MD
only ever answers with a prompt. `A`'s address-and-contents line -- the format
the encoder's parser actually has to read -- needs a run that sends `A` and an
address. That is the same harness with different input, and it is the next
thing.

Until it is captured, the no-guessing rule stands for the `A` line specifically:
its column layout and separators remain unknown, and this document says so
rather than extrapolating them from the prompt.
