# Apollo graphics controllers — findings

The DN3500's monochrome and colour controllers are modelled from `008778-03`
and are not this file's subject; it exists for the **DN4500's Matrox board**,
which no document on disk describes and which no oracle runs.

Status legend: `confirmed` · `open` · `provisional` · `deliberate divergence`

## Sources

| Key | Document |
| --- | --- |
| `[S3K]` | *Domain Series 3000/4000 Technical Reference*, 008778-03, Aug 1987 — ch. 10 is the graphics chapter |
| `[CFG]` | *HP-Apollo Products Configuration Guide*, Dec 1989, and the Jul 1990 quick reference |
| `[ROMMX]` | `roms/firmware/4500_Matrox_013748_04.bin` — 64 KB, Apollo part 013748 rev 04 |
| `[ROM4500]` | `roms/firmware/4500_BOOT_13167_02_MD7R.0.32.bin` — the DN4500's own boot PROM |

## Why this file starts from a ROM

`CLAUDE.md`'s resolution order is reference → web → oracle, and for this board
**all three run out**, which is recorded here so nobody repeats the search:

- `[S3K]` chapter 10 is the graphics chapter and covers the **DN3000 and
  DN4000** controllers only — 4-plane colour, two 1280x1024 monochrome, 8-plane
  colour. It gives PCB dimensions, display cables and supply voltages: no
  register map, and no Matrox board. Its Table 2-6 graphics ranges (`0A0000`,
  `0C0000`, `0E0000`, `FA0000`) contain none of the addresses below.
- The **web** has no register-level material for Apollo part `013748`. What
  exists is sales listings and unrelated modern Matrox parts.
- There is **no oracle**: MAME does not register the 4500 variants at all.

So the board's own firmware is the only specification, which is exactly where
`RING.md` and `ETHERNET.md` both ended up. The method that worked twice is the
method here.

## Established

| # | Finding | Source | Status |
| --- | --- | --- | --- |
| 1 | **`[ROMMX]` is an ordinary Apollo option ROM and `tools/ring-rom/disasm.py` reads it unchanged.** Magic0 `335E91B6`, `hdr_ver` 1, `length` `$1D8E`, checksum **VALID**, and a four-record entry table: `id=1` at `+21A` (init), `id=2` at `+3AA`, `id=3` at `+388`, `id=4` at `+4D4`. Same header the ring and 3c505 ROMs carry | `[ROMMX]` header | confirmed |
| 2 | **It is the only `C000A0B7` image this project holds**, which is the option-ROM class the boot PROM's early scan accepts on **magic alone** — `RING.md` finding 16's `$106A` matcher, which applies no `field_1a`/`d0` class check. `RING.md` 59b had concluded no such ROM was held; finding 70 there retracts that. A sweep of `magic1` across all seventeen ROMs in `roms/firmware/` gives exactly one hit | `xxd -s 4 -l 4` over `roms/firmware/*.bin` | confirmed |
| 3 | **Measured: the scan accepts it and the machine executes it.** With `--option-rom roms/firmware/4500_Matrox_013748_04.bin` a boot ends at `final PC 000805A8` — inside the image, at `080000 + $5A8`. This is the first time this project has seen the early accepting scan take a ROM, and it answers `RING.md` 59a's open half: that scan runs early, in an ordinary boot, with no calendar selector needed | `--option-rom`, `--boot-limit 40000000` | confirmed |
| 4 | **The ROM carries microcode for the board, and says so.** `+B22` holds the ASCII `ID: GAO Boot Microcode, Rev 0.00`, loaded by `lea.l $b22(pc),a1` at `$500`; and the code contains longword bulk-transfer loops `move.l (a3)+, $d40000.l` at `$3DE` and `$4B2`. So the host does not merely configure this board — it **downloads a program to it**, which is why a register-level model alone will not be the whole of it | `[ROMMX]` `$500`, `$3DE`, `$4B2` | confirmed |
| 5 | **Three register blocks, extracted mechanically from the listing.** Every absolute long operand in the image resolves to one of three bases, with the counts: `$DA0006` (14), `$D40000` (8), `$D80004`/`$D80005`/`$D80008` (7), `$DA0007` (1). Two further operands — `$85838181` and `$514CC005` — are implausible as addresses and are data the linear sweep decoded as instructions; they are named so a later reader does not chase them | `[ROMMX]`, exhaustive scan of absolute long operands | confirmed |
| 5a | **`$DA0000` is a block *base*, proved by the firmware taking it as one.** `$4D8` does `movea.l #$da0000, a3` and then writes through `(a0)+` from it: `move.l #$b00000c0,(a0)+` at `+0` and `move.w #$c00,(a0)` at `+4`. So `$DA0006`/`$DA0007` are `+6`/`+7` within a block at `$DA0000`, the same shape as the ring's `$59000` base with its `+400`-`+406`. The block is therefore at least eight bytes wide and the two known offsets are not isolated registers | `[ROMMX]` `$4D8`-`$4EA` | confirmed |
| 6 | **`$DA0006` is command *and* status, byte-wide, with three polled bits.** Written as a word `#$08DB` (spanning `+6`/`+7`) at `$222` and `$4EA`; written as bytes `#$9` to `+6` and `#$3` to `+7` at `$52A`/`$532`; read back with `and.b`/`move.b` at eight sites and tested with `btst.b #$5` (`$2F0`, `$304`, `$4C2`), `btst.b #$4` (`$3BA`) and `and.b #$8` (`$59E`). So bits 3, 4 and 5 are polled conditions and the same address takes commands — the arrangement `RING.md` 48 found on the ring's `+402` | `[ROMMX]`, all fourteen sites | confirmed |
| 7 | **`$D40000` is a bidirectional data port.** Its address is taken (`lea.l $d40000.l,a2` at `$2A2`), it receives the two `move.l (a3)+` download loops of finding 4, and it takes four literal words in sequence — `#$5AA5`, `#$A534`, `#$1744`, `#$1345` at `$578`-`$590` — after which `$5D6` reads it back with `move.w $d40000.l,d1`. Write-a-pattern-then-read-it-back is a **signature or presence check**, and `5AA5`/`A534` are the walking pattern such checks use | `[ROMMX]` `$2A2`, `$3DE`, `$4B2`, `$578`-`$5D6` | confirmed |
| 8 | **`$D80004`/`$D80005`/`$D80008` are a third block with a ready bit.** `#$80` is written to `+5` at `$298` and `$2B6`; longwords go to `+8` (`move.l d0` at `$334`, `move.l #$FFFFFFFF` at `$35C`); and `btst.b #$7, $d80004.l` polls bit 7 at `$316`, `$342` and `$36A`, once after each transfer. A longword port with a ready bit polled after every write is the shape of a **bulk memory path**, and `$FFFFFFFF` is the all-ones pattern a memory test writes | `[ROMMX]` `$298`-`$36A` | confirmed |
| 9 | **Where the run stops, and why.** `$598`-`$5AE` is a bounded poll: `move.l #$f00000,d1` loads a **15,728,640**-iteration budget, then `and.b $da0006.l,d0` against `#$8` with `beq` — waiting for **bit 3 to read clear**. Nothing in this core decodes `$DA0006`, so it answers `FF` as an undriven AT bus does, bit 3 reads set, and the loop spins its whole budget. That is the measured stop at `+5A8` in finding 3, explained by the firmware's own code rather than guessed | `[ROMMX]` `$598`-`$5AE` | confirmed |
| 10 | **The DN4500's own boot PROM names none of these addresses.** `[ROM4500]` contains no absolute operand `00D40000`, `00DA0000` or `00DA0006`, searched exhaustively. So the three blocks are the *board's*, reached only through its option ROM, and the host PROM knows the card solely through the option-ROM scan — consistent with finding 2 | `[ROM4500]`, byte search | confirmed |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | Which of `$D40000`, `$D80000` and `$DA0000` is the frame buffer, and its geometry | The download loops of finding 4 and their source pointers; `[CFG]`'s Series 4500 display options bound the resolutions |
| B | What the microcode of finding 4 *is* — a coprocessor's program, or a display list | It is inside `[ROMMX]`, so its extent is recoverable from the loop bounds at `$3DE` and `$4B2` |
| C | Whether these addresses decode identically on a real DN4500 | Finding 3's measurement used the **DN3500** PROM and map, because `identity-boot.sh` does. The addresses are facts about the board; the model's map is a separate question and `[ROM4500]` plus the model table are where it comes from |
| D | Bit meanings of `$DA0006` bits 3, 4 and 5 | The firmware's own polls (finding 6) constrain their *polarity* at each site; the ring's method — satisfy one poll, re-run, read the next failure — applies unchanged, and needs the controller modelled far enough to answer |

## Divergences from the oracle

None possible: MAME does not register the DN4500 or DSP4500, so this board has
no runnable reference and every figure here cites `[ROMMX]` by address.
