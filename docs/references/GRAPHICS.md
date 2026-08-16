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
| 4 | ~~The ROM carries microcode, and the `move.l (a3)+, $d40000.l` loops at `$3DE`/`$4B2` are what download it~~ — **the conclusion stands, the mechanism was wrong; corrected by 4a and 4b.** The board does take a downloaded program, but not through those loops, which turn out to be small tables. Recorded rather than rewritten because the mistake was reading a bulk-transfer *shape* and assuming its size | superseded | retracted |
| 4a | **Those two loops are one 16-longword table, entered at two points.** `$3D6` does `lea.l $96(pc),a3` with `#$6` in `d0` (7 longwords) and `$4AA` does `lea.l $72(pc),a3` with `#$f` in `d4` (16 longwords) — and **both end at exactly `+B2`**, so the second writes the whole table and the first only its last seven. 64 bytes, not a program. The values are `00000400` (1024), `050003D0` twice, `05E00020`, `00000010` — the shape of a **CRTC parameter block**, which is what a graphics board is given before it will scan | `[ROMMX]` `$3D6`-`$3E4`, `$4AA`-`$4B8`, bytes `+72`-`+B2` | confirmed |
| 4b | **The real download is at `$504`, and the arithmetic closes on the header.** `$4FA` loads `d0 = $935` and `$500` does `lea.l $b22(pc),a1`, then `move.w (a1)+,(a0)` / `dbra` — **2358 words = 4,716 bytes** from `+B22`, written to `(a0)`, which `$4F2` took from `a3` = **`$DA0000`**, and which is *not* incremented. So it is a bulk feed to a fixed port. And `$B22 + 2358x2 = $1D8E`, **exactly the `length` field in the ROM header** (finding 1): the microcode runs from `+B22` to the last byte of the checksummed image. Two independent numbers meeting is what makes this an identification rather than a reading | `[ROMMX]` `$4FA`-`$506`, header `length` | confirmed |
| 4c | **So the ASCII is the image's own header, not a message.** `ID: GAO Boot Microcode, Rev 0.00` sits at `+B22` — the first bytes of what is downloaded — which is why it reads `ID:`. Nothing prints it: no console call takes it, and the only instruction that touches it is the copy loop. A model that treated it as a diagnostic string would have missed the 4.7 KB behind it | `[ROMMX]` `+B22`, `$504` | confirmed |
| 5 | **Three register blocks, extracted mechanically from the listing.** Every absolute long operand in the image resolves to one of three bases, with the counts: `$DA0006` (14), `$D40000` (8), `$D80004`/`$D80005`/`$D80008` (7), `$DA0007` (1). Two further operands — `$85838181` and `$514CC005` — are implausible as addresses and are data the linear sweep decoded as instructions; they are named so a later reader does not chase them | `[ROMMX]`, exhaustive scan of absolute long operands | confirmed |
| 5a | **`$DA0000` is a block *base*, proved by the firmware taking it as one.** `$4D8` does `movea.l #$da0000, a3` and then writes through `(a0)+` from it: `move.l #$b00000c0,(a0)+` at `+0` and `move.w #$c00,(a0)` at `+4`. So `$DA0006`/`$DA0007` are `+6`/`+7` within a block at `$DA0000`, the same shape as the ring's `$59000` base with its `+400`-`+406`. The block is therefore at least eight bytes wide and the two known offsets are not isolated registers | `[ROMMX]` `$4D8`-`$4EA` | confirmed |
| 6 | **`$DA0006` is command *and* status, byte-wide, with three polled bits.** Written as a word `#$08DB` (spanning `+6`/`+7`) at `$222` and `$4EA`; written as bytes `#$9` to `+6` and `#$3` to `+7` at `$52A`/`$532`; read back with `and.b`/`move.b` at eight sites and tested with `btst.b #$5` (`$2F0`, `$304`, `$4C2`), `btst.b #$4` (`$3BA`) and `and.b #$8` (`$59E`). So bits 3, 4 and 5 are polled conditions and the same address takes commands — the arrangement `RING.md` 48 found on the ring's `+402` | `[ROMMX]`, all fourteen sites | confirmed |
| 7 | **`$D40000` is a bidirectional data port.** Its address is taken (`lea.l $d40000.l,a2` at `$2A2`), it receives the two `move.l (a3)+` download loops of finding 4, and it takes four literal words in sequence — `#$5AA5`, `#$A534`, `#$1744`, `#$1345` at `$578`-`$590` — after which `$5D6` reads it back with `move.w $d40000.l,d1`. Write-a-pattern-then-read-it-back is a **signature or presence check**, and `5AA5`/`A534` are the walking pattern such checks use | `[ROMMX]` `$2A2`, `$3DE`, `$4B2`, `$578`-`$5D6` | confirmed |
| 8 | **`$D80004`/`$D80005`/`$D80008` are a third block with a ready bit.** `#$80` is written to `+5` at `$298` and `$2B6`; longwords go to `+8` (`move.l d0` at `$334`, `move.l #$FFFFFFFF` at `$35C`); and `btst.b #$7, $d80004.l` polls bit 7 at `$316`, `$342` and `$36A`, once after each transfer. A longword port with a ready bit polled after every write is the shape of a **bulk memory path**, and `$FFFFFFFF` is the all-ones pattern a memory test writes | `[ROMMX]` `$298`-`$36A` | confirmed |
| 9 | **Where the run stops, and why.** `$598`-`$5AE` is a bounded poll: `move.l #$f00000,d1` loads a **15,728,640**-iteration budget, then `and.b $da0006.l,d0` against `#$8` with `beq` — waiting for **bit 3 to read clear**. Nothing in this core decodes `$DA0006`, so it answers `FF` as an undriven AT bus does, bit 3 reads set, and the loop spins its whole budget. That is the measured stop at `+5A8` in finding 3, explained by the firmware's own code rather than guessed | `[ROMMX]` `$598`-`$5AE` | confirmed |
| 10 | **The DN4500's own boot PROM names none of these addresses.** `[ROM4500]` contains no absolute operand `00D40000`, `00DA0000` or `00DA0006`, searched exhaustively. So the three blocks are the *board's*, reached only through its option ROM, and the host PROM knows the card solely through the option-ROM scan — consistent with finding 2 | `[ROM4500]`, byte search | confirmed |

| 11 | **The routine's verdict, and both ways it fails.** `$5A8`-`$5CE` is the tail of the routine ending at `$5E0`, and it stores `d3`: **`0` on pass, `$FFFF` on fail**. Two conditions reach the pass arm, both of `$DA0006` — **bit 3 must go clear** within the 15,728,640-iteration budget of finding 9, and **bit 6 must read clear** when tested once at `$5B8`. A set bit 6 stores `$FFFF` by the same instruction the timeout does, so the two failures are indistinguishable from the verdict alone. After the verdict it reads `$D40000` 4003 times and **discards every value** (`move.w $d40000.l,d1`, `d1` never examined), which is a drain rather than a check | `[ROMMX]` `$5A8`-`$5E0` | confirmed |
| 12 | **The device is built, and the microcode download completes exactly as finding 4b predicts.** `src/core/device/ap_matrox.*` decodes the three blocks and answers `$DA0006` as zero — which satisfies finding 11's two conditions and asserts nothing else, the restraint `RING.md` 62 used. Fitted with `--matrox`, a boot drives it **114,503 reads and 4,746 writes**, and the run's own registers close the loop: `a1` ends at **`00081D8E`**, the option ROM base `080000` plus exactly the `length` `$1D8E`. So the copy loop transferred its 2358 words and stopped on the last byte of the checksummed image, measured from the machine rather than computed | `--matrox --option-rom`, `matrox_suite` | confirmed |
| 12a | **But the boot does not proceed, and that is the honest state of this item.** With the ROM fitted the machine stops at `PC 000061F4` in the *boot PROM* and prints **no console output at all** — no `Self tests in progress` — and a 350 M run has byte-identical device counts and the same PC as a 60 M one, so it is genuinely stuck rather than slow. The firmware's verdict is therefore **not observable yet**: nothing reports pass or fail. What the device has earned so far is its unit tests and finding 12's download, not a working boot | 60 M and 350 M runs compared | open |
| 12b | ~~The likely reason is question C: a DN4500 board's ROM on a DN3500 machine and map~~ — **REFUTED by 12d, which is what the experiment was for.** Recorded rather than deleted: it was a plausible suspicion, it was named as a suspicion, and it was wrong | superseded | retracted |
| 12d | **Run on the DN4500's own PROM and map, the failure is identical — so the machine was never the problem.** `--model dn4500 --boot-prom 4500_BOOT_13167_02_MD7R.0.32.bin` with the card and its ROM prints **no console output**, stops in the boot PROM at `PC 0000620A`, and leaves `d3 = $0000FFFF`, `a0 = $00DA0006`, `a1 = $00081D8E` — the same shape as the DN3500 run's stop at `61F4`. The **control** is what makes it evidence: the same DN4500 PROM *without* the card boots normally, printing `Self tests in progress` and reaching `Winchester Disk Test # 0` by 60 M instructions. So fitting this board is what stops the boot, on the machine the board belongs to | `--model dn4500`, with and without the card | confirmed |
| 12e | **And `a1` proves the ROM ran to completion before anything went wrong**: `00081D8E` is the image base plus its whole `length`, so the microcode copy finished on both machines. Whatever fails, fails *after* the download and leaves the PROM looping rather than the ROM. Both stops are at PROM addresses (`61F4`, `620A`) in two different PROMs, which points at what the PROM does *with* an accepted ROM rather than at the ROM's own code | 12, 12d | confirmed |
| 12f | ~~The 64 KB image answers the scan's magic at all four 4 KB slots, so the PROM finds four cards where there is one~~ — **REFUTED, and cheaply.** `--boot-watch-read 82000` reports the third slot read **0 times**: the scan stops once a card answers at `080000`. Two hypotheses tested and two refuted, which is the point at which this project's own rule says re-frame rather than guess a third | measured | retracted |
| 12g | ~~It is a runaway: the PC walks PROM data executed as `nbcd.b d1`~~ — **WRONG, and retracted the same way it was made.** The bytes at `$61F8`-`$6216` really are fifteen `4801`s, but `$6218` is `subq.l #$1,d0` and `$621A` is `bgt.b $61FA`: they are the **body of a delay loop**, with `$61F4` pushing `d1` and `$61F6` doing `lsr.l #$2,d0` to scale the count. Fifteen cheap instructions and a countdown is how firmware burns calibrated time. The tell I ignored: the machine enters this region at instruction **606,079**, long *before* the option ROM runs, so it was never reachable only by a runaway | `[ROM4500]` `$61F4`-`$621E`, raw bytes | retracted |
| 12h | **What is actually happening: the PROM retries the graphics initialisation forever.** `--boot-log-pc 61F4` over 120 M instructions catches the delay entered **23 times and still going**, alternating between two callers — return addresses `00005EE0` and `00005EF8` — with `d0` alternating `$1E8480` (**2,000,000**) and `$7A120` (**500,000**), and `d1 = $0D` throughout. The first call is at instruction 1,167,443, **six instructions after the option ROM's `rts`** at 1,167,437. So the boot is not stuck and not running away: it is waiting, at length, on a condition this core never satisfies, and retrying | `--boot-log-pc 61F4`, 120 M | confirmed |
| 12i | **And the ROM's own call is clean, which localises the fault.** `--boot-log-pc 805E0` shows the routine's `rts` reached exactly once, returning to `$4422` — and `[ROM4500]` `$4420` is `jsr (a0)` with `$4422` an `rts`, so the PROM called the entry and got control back normally. Nothing is wrong with the call, the return, or the stack. What follows it is the retry loop above, so the condition being waited on is set *after* the entry returns, by hardware this core does not yet model | `[ROM4500]` `$4410`-`$4422`, `--boot-log-pc 805E0` | confirmed |
| 12j | **Read, and it tests nothing: it is the firmware's error display.** Decoded from raw bytes, because the linear sweep is misaligned there: `$5ED6` delays 2,000,000, `$5EE0` takes `$92(a5)` masked to its low nibble and writes it to **`$00010100`**, `$5EEE` delays 500,000, `$5EF8` writes the *unmasked* byte to the same place, and `$5F00` is `bra.b $5ED6`. No test and no exit — an unconditional loop alternating two values at one address | `[ROM4500]` `$5ED6`-`$5F00`, raw bytes | confirmed |
| 12k | **And `$00010100` is the diagnostic LED register, which this project already modelled and already reports.** `AP_BOARDREG_CPU_CONTROL_ADDR`, and `ap_boardreg.h` names **this very loop** — "the firmware also writes this register *directly* in places -- `005EC8` and `005ED8` do, in the error loop". `FINDINGS.md` C109 found the post routine. So three turns of diagnosis ended somewhere the codebase had documented, and the answer was one line of the boot report: **`posted codes`**. The lesson is the recurring one — read the report before disassembling | `ap_boardreg.h`, `FINDINGS.md` C109 | confirmed |
| 12l | **The codes, with controls, and they say the device is not what fails.** Healthy DN4500, no card: `FF EF DF FE EE DE CF BF AF 9F **8F FE**` — the post routine complements, so that is 00,10,20,01,11,21,30,40,50,60,**70**. With the option ROM and **no** device: `… 9F **ED**`. With the ROM **and** the device: `… 9F **ED 0D ED 0D ED**`. So a machine that should post `70` after `60` instead enters the error loop displaying `ED`/`0D` — and it does so **whether or not our Matrox registers answer**. Fitting the device changes the blink but not the verdict | three runs, `--model dn4500` | confirmed |
| 12m | **Which relocates the problem off the device entirely.** `ap_matrox` was built to satisfy finding 11's two status bits, and it does; but the PROM fails with the registers answering exactly as before. So what is wrong is *not* the three blocks' behaviour | 12l | confirmed |
| 13 | **The failing code is `21`, decoded with a transformation this project had already recovered.** `PROJECT_STATUS.md` records the post routine: `ror.b #4,d1` then `not.b d1`, then store and display. Running the displayed `ED` back through it — `not ED = 12`, swap nibbles — gives **`21`**; the healthy machine's next code `8F` is **`07`** the same way. So the boot stops in the check that posts `21`, and the one it should have reached posts `07` | `[ROM4500]` post routine, `PROJECT_STATUS.md` | confirmed |
| 13a | **And that check is the *built-in* display controller's, not the Matrox board's.** `move.b #$21,d1` is at `[ROM4500]` `$6962` — found by searching for `123C0021` rather than by reading — and what follows is `movea.l #$0005E801,a2`, `movea.l #$000A0000,a1`, `cmpi.b #$8,(a2)`. `0005E801` is the colour controller's ID register this core already models and `0A0000` is `[S3K]` Table 2-6's graphics memory. So fitting *any* option ROM sends the PROM down a graphics path that tests the machine's own display first | `[ROM4500]` `$6956`-`$697C` | confirmed |
| 13b | **So the missing piece was a display, and fitting one moves the boot a long way.** `ap_graphics` answers its ID at `05E801` only when a screen is fitted, and every run above had `display none` — so `cmpi.b #$8` compared against the `FF` of an empty decode. With `--screen c8p` the posted codes go `… 9F ED **DD 9D 8D 7D 6D 5D FC**` instead of stopping at `ED`, and control reaches **the Matrox ROM's own code** at `+2F8`/`+304`. Six more checks passed, on a one-flag change | `--screen c8p`, `--model dn4500` | confirmed |
| 13c | **The next assertion: bit 5 must read *set*.** `$2EC`-`$310` loads `d0 = $FFF0`, polls `btst.b #$5,$da0006.l` and **exits early on `bne`**, with a `divs.w` between the two polls as a delay and a `dbra` bound. The opposite polarity to bits 3 and 6, which is why answering the whole register zero satisfied the first routine and stalled this one | `[ROMMX]` `$2EC`-`$310` | confirmed |
| 14 | **Satisfied, and the boot no longer fails.** `$DA0006` now reads `$20` — bit 5 set, bits 3 and 6 clear, which is exactly the three conditions measured and nothing more. Bit 4 stays clear because its `btst` at `$3BA` has still not been reached. The posted codes go from stopping at `FC` to `… 5D FC **8F FE FB FA F9 F8**` — six further checks — and the machine ends at `PC 000007A2`, inside `FINDINGS.md` C109's **console-selection poll** at `00078E`-`0007AE`, which that finding describes as where "an idle machine sits ... forever" waiting for a keystroke. So it is idle-and-waiting, not failed | `--matrox --option-rom --screen c8p`, 200 M | confirmed |
| 14a | **Checked against the control, and stated precisely rather than generously.** The same machine with a screen and **no card** posts three codes further — `… F8 **E8 7F F7**` — and ends at `PC 00000794`, in the same poll region. So the two are **not identical**: fitting the board still costs three late codes. What is established is that the board no longer *stops* the boot, which is what every run before this one did | control run, 200 M | confirmed |
| 14b | And the graphics code sequence `ED DD 9D 8D 7D 6D 5D FC` appears in the **no-card** run too, once a screen is fitted. So those eight are the *display's* own initialisation, not the option ROM's — corroborating the existing `PROJECT_STATUS.md` finding that a display-fitted boot runs a path a console-only boot never enters, and confirming 13a's reading that the failing check was the built-in controller's | control run | confirmed |
| 15 | **The item's own verification now runs, and it fails — usefully.** `--screenshot` with a screen fitted and **no card** produces the DN4500's whole boot on screen: `SELF TESTS IN PROGRESS.`, the keyboard, CPU, four memory modules and two Winchester tests, `NETWORK DRIVER SEARCH STARTED...`, `LOADING SELF_TEST DIAGNOSTICS FROM BOOT DEVICE.`, `COULD NOT LOAD /SAU7/SELF_TEST.` and a `>` prompt, in 1024x800 8-plane. **With the Matrox board fitted the same run is entirely black.** So the card does not merely cost three posted codes — it costs the picture | two `--screenshot` runs, 200 M | confirmed |
| 15a | **And the counters say where the picture went.** Fitting the card takes the built-in controller from **1,697,852 blit cycles / 9,912,744 plane writes** down to **655,372 / 1,572,904**, while the Matrox blocks go from 114 K reads to **1,513,193 reads and 16,618 writes**. The console output is being driven at the Matrox board instead of the built-in display — which is what an option ROM for a graphics card is *for*. So this is not a defect to hunt but the card doing its job into a frame buffer this core does not have | same two runs | confirmed |
| 15b | **Which answers question A by elimination and sets the real next step.** The three blocks modelled are ports — 16,618 writes is four orders short of a 1024x800 8-plane frame — so the pixels are going somewhere else entirely, and finding the write path is what a picture depends on. `[S3K]` Table 2-6's graphics memory ranges (`0A0000`-`0BFFFF`, `0C0000`-`0DFFFF`, `0E0000`-`0FFFFF`, `FA0000`-`FDFFFF`) are the documented candidates and none is yet decoded for this board | 15a | confirmed |
| 16 | **Found, and it is one of Table 2-6's four: the pixels go to `0C0000`-`0DFFFF`.** The run's own census puts **30,754,191 reads and 50,744 writes** in `AT bus (empty slot)` — the undecoded window — and the board's first-address tracker names the **first write as `000C63AF`**. That falls inside `[S3K]` Table 2-6's `0C0000 - 0DFFFF`, **"ALTERNATE MONO GRAPHICS MEMORY SPACE"**. So the reference named the candidate and the measurement landed in it, which is the agreement this project asks for rather than either alone | region census + `first write`, 200 M | confirmed |
| 16a | **And the board's own ROM corroborates it independently.** `[ROMMX]` `$2E0` is `movea.l #$c63b2,a3` — a pointer into the same range, three bytes from the first write the machine actually made, and one of only two absolute constants in the image outside the three register blocks (finding 5 listed `$85838181` and `$514CC005` as data; this one is neither). Two sources that have not met: a 1987 manual's allocation table and a 1990 option ROM's own immediate | `[ROMMX]` `$2E0` against 16 | confirmed |
| 16b | **What remains before a picture, stated so the item is not read as nearly done.** The *range* is settled by measurement; the **layout is not** — how many planes, what pitch, and whether `0C0000` is the frame's origin or a window onto more. Finding 4a's parameter table carries `00000400` = 1024 and `050003D0`, which is where the geometry will come from | 16 | open |
| 16c | **CORRECTION to 16's citation, and it matters more than the wording.** Table 2-6 was read from the page image and says exactly what the extraction said — but its own heading is *"Table 2-6 lists the physical address space allocation for the **DS3000** system"*, and its title is **16-MB**. This machine is a **DN4500** on the 32-bit `DS4000` map. So the range `0C0000`-`0DFFFF` is where the writes **measurably go**, and "ALTERNATE MONO GRAPHICS MEMORY SPACE" is a *different model's* name for that range, not this one's. `RING.md` 43b already records this exact hazard against this exact table — "Table 2-6 is the DS3000's, and the Series 3500 has no equivalent table on disk" — and I cited it as though it were the DN4500's anyway | `[S3K]` Table 2-6 page image, p. 2-17 | confirmed |
| 16d | **What survives the correction, and what does not.** Surviving: the measurement (30.7 M reads and 50,744 writes in the undecoded window, first write `000C63AF`) and the ROM's own `movea.l #$c63b2,a3` at `$2E0` — two witnesses to *where*, neither of which depends on any table. Not surviving: that the range is called "alternate mono", and with it 16b's "alternate **mono** on an 8-plane colour machine" puzzle. **The nearest 32-bit map held is `019411-A00`'s DS5500 Table 2-5**, and it is the reference to read before naming this range | 16, 16a, 16c | superseded by 17 |
| 17 | **Read, and the name stands after all — 16c's retraction was one step too far.** `019411-A00` Table 2-5, the **DS5500 256-MB** allocation and the only 32-bit Apollo map on disk, gives `0C0000-0DFFFF` as **"ALTERNATE MONO GRAPHICS MEMORY SPACE OR SINGLE-BOARD RING CONTROLLER MEMORY SPACE (D0000 - DFFFF)"**. Same name as the DS3000's 16-MB table, on the map class this core actually uses for the DN3500/4500/5500 (`DS4000_MAP`). So the label carries across address-space sizes, and the measured first write `000C63AF` is in the *graphics* half, below the ring controller's `D0000`. Corroborated further by the same table's `1000000- MAIN MEMORY`, which is `AP_BOARD_RAM_BASE` exactly | `019411-A00` Table 2-5, page image | confirmed |
| 17a | **So the Matrox board presents as the *alternate monochrome* controller**, which the table makes a real distinction rather than a quirk: `0E0000-0FFFFF` is "ALTERNATE **COLOR** GRAPHICS MEMORY SPACE" and the machine's own 8-plane colour display is at `0A0000`. The card writes to neither of those. Odd for a board on a colour workstation, and it is the *evidence* rather than a reading — the writes are where they are | 16, 17 | confirmed |
| 18 | **RETRACTED: `0C0000` is not where the pixels go, and finding 16 read one address as if it were fifty thousand.** 16 took the run's `first write 000C63AF` as the location of the frame. Decoding `0C0000`-`0DFFFF` for real settles it: the range receives **6 writes** in a whole boot, while **50,738** still land in the undecoded window. The first empty-slot write was simply the first, and the bulk was never there. The error is this project's oldest — a single recorded value generalised to a population — and the instrument that catches it (`first seen`, a list of distinct addresses) was in the same report all along | `--matrox-screenshot`, 200 M | confirmed |
| 18a | **Where they actually go, from the list rather than one sample: `0093D000`-`0093DD29`.** The report's `first seen` line names sixteen distinct addresses and they are all `0093D0xx`-`0093D3xx`, with `first write 0093D000` and `last write 0093DD01`, and **108,035 further distinct addresses not recorded** because the tracker holds sixteen. That is AT bus memory space in Table 2-5's `100000-FFFFFF`, nowhere near a documented graphics range | same run | confirmed |
| 18c | **The extent, measured with a new instrument, and it is not a frame buffer at all.** `first`/`last` are chronological, so neither bounds a region; the board now records the lowest and highest empty-slot address as well. The run gives **write span `0004D402..0093DD3F`** and **read span `0004D400..00FFF003`** — crossing AT *I/O* space and AT *memory* space, nearly the whole window. Against that, 59,633 reads and 50,738 writes over **108,051 distinct addresses**: about **one access per address**. A region written once per address across the whole window is a **scan**, not a frame being drawn | extent tracker, 200 M | confirmed |
| 18d | **Which withdraws finding 15a's attribution, the last thing I had been building on.** 15a read the built-in controller's plane writes dropping from 9,912,744 to 1,572,904 as "the console output moved to the card". The drop is real and measured; the *destination* was inference, and 18c shows the empty-slot traffic is a one-touch scan rather than pixels going anywhere. So where the missing plane writes went is **open**, and the Matrox card is no longer the answer to it. Findings 16 through 17b were a chain built on that attribution and on one sampled address; what remains standing from this whole line is the register map (5-9), the firmware's assertions (11, 13c), and that the screen is black (15) | 15a against 18c | confirmed |

| 18b | **What that does and does not settle.** The traffic's *location* is now measured rather than inferred — and it is **not** a documented graphics window, so 17 and 17a's identification of this board as the "alternate mono" controller rests on the `0C0000` reading that 18 just removed and should be treated as unsupported until something else carries it. What survives untouched is finding 15a's counting: the console output moved to this card, wherever its frame is. The span `0093D000`-`0093DD29` is about 3.3 KB against 50,738 writes, so it is written many times over and is more likely a **window or a port** than a linear frame -- which is the next thing to establish, and not by reading one address | 18, 18a | open |

| 17b | **And the arithmetic closes on a geometry** — for a range that finding 18 has since shown is not the frame, so this stands as arithmetic and not as a location. `0C0000`-`0DFFFF` is **128 KB = 1,048,576 bits**, which is exactly **1024 x 1024** at one bit per pixel — the depth "mono" implies. Finding 4a's parameter table, written to `$D40000` before any of this, carries `00000400` = **1024**. Two independent numbers meeting, which is the standard this file has used throughout. Recorded as the **hypothesis to test**, not as established: what would settle it is rendering that range at 1024x1024x1 and reading the result, since a wrong pitch produces a sheared but still legible picture and a right one does not | Table 2-5 extent, finding 4a | open |
| 12c | **The reference boot is untouched, checked rather than assumed.** `tools/identity-boot.sh` with no new flags returns state hash `A354786119A3931D`, the reference — so the new region in the enum, the new device and its decode change nothing for a machine that does not fit the card | measured | confirmed |

## Open

| # | Question | How it will be answered |
| --- | --- | --- |
| A | Which of `$D40000`, `$D80000` and `$DA0000` is the frame buffer, and its geometry | **None of them, and the frame is at `$900000` -- findings 20-20b.** All three are ports. The board's own `ENTRY_03` clears a 256 KB raster at `$900000`, 1280x1024 at 1bpp on a 256-byte stride, and `019411-A00` Table 2-5 puts that address in AT bus memory space. **Still open (20c): nothing has been seen to execute `ENTRY_03`**, so the screenshot is black for a new reason -- a decoded frame with nothing drawn into it |
| B | ~~What the microcode is, and its extent~~ | **ANSWERED by 4b: 4,716 bytes at `+B22`, downloaded word by word to the fixed port `$DA0000`**, ending exactly on the header's `length`. What it *does* is a question about an unknown target processor and is **not** on this project's path: nothing needs to execute it, only to accept it. What matters for the model is that `$DA0000` swallows 2358 words without complaint |
| C | Whether these addresses decode identically on a real DN4500 | Finding 3's measurement used the **DN3500** PROM and map, because `identity-boot.sh` does. The addresses are facts about the board; the model's map is a separate question and `[ROM4500]` plus the model table are where it comes from |
| D | Bit meanings of `$DA0006` bits 3, 4 and 5 | The firmware's own polls (finding 6) constrain their *polarity* at each site; the ring's method — satisfy one poll, re-run, read the next failure — applies unchanged, and needs the controller modelled far enough to answer |

## Divergences from the oracle

None possible: MAME does not register the DN4500 or DSP4500, so this board has
no runnable reference and every figure here cites `[ROMMX]` by address.

## Finding 20: the ROM clears a 1280x1024 raster at `$900000`

**A static census of `[ROMMX]`, which had not been taken.** Findings 3 and 15
counted addresses the ROM uses as *absolute operands* and writes the machine
made at run time. Neither sees an address loaded into a register, and that is
where this was hiding: `movea.l #$900000, a1` at `$38C` and `movea.l #$900000,
a4` at `$3EC`. `$900000` appears nowhere as an absolute operand, which is why
every previous pass missed it.

`ENTRY_03`, the option ROM's third entry point (finding 1 puts `id=3` at
`+388`), is a **clear-screen loop** and its constants give the geometry outright:

```
000388  ENTRY_03
00038C  movea.l #$900000, a1     ; base
000392  move.w  #$3ff, d1        ; 1024 lines
000396  moveq   #$60, d2         ; 96 bytes skipped per line
000398  move.w  #$27, d0         ; 40 longwords
00039C  clr.l   (a1)+            ; = 160 bytes cleared
00039E  dbra    d0, $39c
0003A2  adda.l  d2, a1           ; -> 256-byte stride
0003A4  dbra    d1, $398
```

160 bytes is **1280 pixels at one bit per pixel**; 160 + 96 is a **256-byte
stride**, i.e. 2048 pixels; 1024 lines at 256 bytes is **262,144 bytes = 256
KB**. Every one of those is already in this core for a different board: the
DN3500's 19-inch monochrome controller is `width = 1280, buffer_width = 2048,
height = 1024` in `ap_graphics.c`, and `[S3K]` §10.2 gives that controller
"256-KB image memory". It is also exactly the panel the model table gives the
DN4500 and the DN3550, `mono 1280x1024`.

So the *geometry* is no longer a hypothesis with arithmetic behind it (17b): it
is the firmware's own loop bounds, and four independent things agree on it.

### 20a: what this does **not** establish, and what settled the rest

**The `0C0000` frame address is superseded, not contradicted.** Findings 16-17b
put the frame there on a measurement -- 50,744 writes into the undecoded AT
window, the first at `000C63AF` -- plus `019411-A00` Table 2-5 naming that range
"ALTERNATE MONO GRAPHICS MEMORY SPACE". Both remain true. What changed is that
the ROM's use of `$0C63AF` is now readable and is **not the shape of a raster
base**:

```
0002CE  move.w  -(a0), -(a7)     ; push words onto the stack
0002D0  dbra    d0, $2ce
0002D8  movea.l #$c63af, a0      ; a parameter ...
0002E0  movea.l #$c63b2, a3      ; ... and a second, three bytes on
0002E6  jsr     (a7)             ; execute the routine just built on the stack
```

They are arguments to a routine the ROM assembles on the stack and calls, they
are **odd** addresses, and they are three bytes apart -- none of which a frame
base is. So those writes are one routine's byte-wise walk, not a raster fill.

### 20b: the map settles the address, read as a page image

`019411-A00` Table 2-5 is the one 32-bit Apollo allocation on disk, and it puts
**`$900000` inside `100000`-`FFFFFF`, "AT COMPATIBLE BUS MEMORY SPACE"** --
which is exactly where an AT card's memory aperture belongs, and the Matrox is
a card rather than motherboard graphics. The same table gives the motherboard's
own "MONO GRAPHICS MEMORY SPACE" at `FA0000`-`FDFFFF`, 256 KB, and
`0C0000`-`0DFFFF` as *alternate* mono graphics at 128 KB with its upper half
(`D0000`-`DFFFF`) the single-board ring controller.

So three things agree on `$900000`: the ROM writes a 256 KB raster there, the
map says that range is AT bus memory where a card's aperture lives, and the
geometry it clears is the DN4500's own panel. `AP_MATROX_FRAME_ADDR` is
**`0x900000`, 256 KB, 1280x1024 with a 256-byte stride**, and the scanout now
walks the stride rather than a packed bitmap -- ignoring it would shear the
picture progressively down the screen.

### 20c: confirmed by the machine, and the prediction was exact

The gap was that nothing had been seen to **execute** `ENTRY_03`: the boot
PROM's option-ROM scan calls the init entry only, so a boot gave 114,503 reads
and 4,746 writes across the Matrox ports and **0 frame writes**.

`--option-rom-entry N` closes it. The lookup -- walk the image's own entry
table, match the id, take the offset -- is the same for every Apollo option ROM,
so this is the general case of what `--ring-selftest` does for `entry_05`; what
differs between them is the argument list, and `ENTRY_03` takes none, which is
why it is a second harness rather than a parameter on the first.

    option ROM entry roms/firmware/4500_Matrox_013748_04.bin
      entry        080388 (id 3, +388)
      returned     after 85001 step(s)
      frame        163840 byte(s) written at 900000

**163,840 is 1024 x 160 exactly**, which is the number this file predicted
before the harness existed. It returned rather than running out, so the loop
completed. And the count settles the **stride** as well as the base, because
`frame_writes` counts *distinct* bytes: 262,144 - 163,840 is 98,304, which is
1024 x 96 -- the gaps. 160 + 96 is a 256-byte line, 1280 visible pixels inside
2048.

So the frame is `$900000` on all four counts and by three independent routes:
the firmware's instruction stream, `019411-A00`'s allocation table, and now the
machine executing it. Question A is answered and finding 17b's "hypothesis with
arithmetic behind it" is retired.

**What remains, and it is a smaller thing than it was**: the screenshot is a
cleared field rather than a picture, because clearing is all `ENTRY_03` does.
Something must *draw* before there is an image, and on this board that is the
downloaded microcode's job (finding 4b) or the operating system's. That is the
next question and it is no longer about *where* the pixels live.

## Finding 21: the board draws, and the picture confirms everything

`ENTRY_02` at `+3AA` is the board's **character output**, and running it puts
the first real image this project has had out of this card.

It masks its argument to seven bits, returns on `$0D`, and for anything from
`$20` up calls the glyph routine at `$430`. That routine is where the frame
layout is stated a *third* time, independently of finding 20's clear loop:

    000430  cmp.w   #$500, d7    ; right margin at 1280 -- the visible width
    00043C  add.w   #$a, d7      ; a 10-pixel cell
    000440  sub.w   #$20, d1     ; space is glyph 0
    000444  mulu.w  #$e, d1      ; 14 bytes a glyph
    000448  lea.l   $5e2(pc), a0 ; the font
    00045A  move.l  d7, d1       ; and the row offset:
    00045C  clr.w   d1           ;   (d7 & $FFFF0000) >> 8
    00045E  lsr.l   #$8, d1      ;   = row x 256, the stride
    000496  addq.l  #$2, d2      ; d2 = $FE + 2 = 256, the stride again

So `d7` packs the cursor -- pixel column in the low word, row in the high -- and
the font is an **8x14 bitmap at `+5E2` with space first**, which dumps legibly:
`A` is `003c42818181ff81818181000000`.

**Typed through `--option-rom-text`, the board renders it:**

```
..####....#######....######...#.........#..........######....
.#....#...#......#..#......#..#.........#.........#......#...
#......#..#......#..#......#..#.........#.........#......#...
#......#..#......#..#......#..#.........#.........#......#...
#......#..#.....#...#......#..#.........#.........#......#...
########..######....#......#..#.........#.........#......#...
#......#..#.........#......#..#.........#.........#......#...
```

`APOLLO DOMAIN DN4500` at 457 set pixels, ink in rows 1-10 of a 14-row cell and
columns 0-197 for twenty 10-pixel cells. Every number the previous two findings
derived is now visible at once: the base, the stride, the width, the cell.

### 21b: the pitch discriminator, run

Finding 17b set the test when the geometry was still arithmetic: "**rendering is
the discriminator: a wrong pitch shears a picture that is still legible, a right
one does not**". It has power because the two sides are independent -- the
*firmware* chose the layout and writes wherever it likes, while the scanout's
stride is this core's own claim about it -- so a wrong claim must shear.

Run against the drawn line, by rebuilding the raw 256 KB frame from the render
and scanning the **same bytes** out at four pitches:

| pitch | result |
| --- | --- |
| **256** | upright and legible |
| 255 | **sheared**: every row displaced 8 pixels right, staircasing down the screen, still fragmentarily legible -- the exact failure 17b names |
| 160 | collapses to fragments; the visible width with no gap is not the line |
| 128 | double-spaced, alternate rows blank -- half a line, so the 96-byte gap is read as a row of its own |

So 256 is discriminated from its immediate neighbour, from the visible width,
and from a plausible half. The pitch is confirmed by the criterion this file set
for it before the answer was known, and finding 17b's test is now spent rather
than outstanding.

### 21a: two harness bugs, both worth recording

**The cursor is the caller's.** Neither entry initialises `d7`, so an
uninitialised one above `$500` makes the margin test return before drawing:
the first attempt typed twenty characters, wrote nothing, and returned in 53
steps. `--option-rom-text` sets it to the origin and carries it across
characters, which is what a driver does.

**Setting `regs.pc` is not entering a routine.** The second attempt assigned
the PC per character and every call faulted at the entry's own first
instruction -- `AP_M68030_STEP_FAULT`, after *zero* steps -- because the
prefetch pipe still held the previous entry's words. Going through
`ap_machine_reset` flushes it along with everything `[030]` §8.1.1 lists; it
resets the CPU and not the board, so the frame survives and only the cursor has
to be carried by hand. **The harness had been swallowing that status**, which is
why two runs looked like "the ROM does not draw" rather than "the harness never
called it".

## The DN3500 controllers' audit, 2026-08-16

`ap_graphics.*` is not this file's usual subject, but the line-by-line audit of
it belongs somewhere and this is the graphics file.

**No structural defect.** The check that found one in both `RING.md` (69b) and
`ETHERNET.md` (19) — what is implemented and called by nobody — comes back clean
here. `ap_graphics_blit`, `_combine`, `_rop_apply`, `_source_data`, `_rop_for`
and the `CR0`/`CR2` field decoders are each reached from `ap_graphics.c`'s own
memory-cycle path, and `_write`, `_read`, `_memory_cycle`, `_advance`,
`_scanout` and `_decode` are all called from the board or a frontend. The
blitter is genuinely wired to the bus.

**`[S3K]` §10.3.1's change list checks out item by item.** All eleven: four
extra planes, the 8 MHz bus interface, "Device ID changed register to readback
`$0A`" (`AP_SCREEN_COLOUR_8_PLANE` is 10), the 32-bit ROP register, the moved
diagnostic memory request, `D_PLANE` at 8 bits and `S_PLANE` at 3 on the added
82C55A (both encodings kept side by side, since neither source settles which the
board wires), the second miscellaneous control register (`CR3B`, 8-plane only)
and the miscellaneous access register (`CR3A`), and the 256 x 24 lookup table
behind `ap_bt458`. Chapter 10 is otherwise physical — dimensions, cables,
voltages — exactly as the header claims.

### Finding 19: the colour raster is printed in full, and was taken from the oracle

**The one real defect, and it is a resolution-order failure rather than a coding
one.** `ap_graphics.h` recorded that Table 11-3 merely *bounds* the colour
monitors and that the raster was therefore the oracle's
`set_raw(68000000, 1346, 0, 1024, 841, 0, 800)`. **§11.1.4 and Table 11-4, one
page further on, give the colour monitor everything Table 11-8 gives the
monochrome** — every porch, the sync width, both blanking intervals, the frame —
and the prose states the line count outright: "within the composite sync signal,
842 horizontal periods occur for each vertical period".

Taking H-Disp = 15.084 µs as the 1024 visible pixels, so a 14.7305 ns dot:

| | duration | pixels/lines |
| --- | --- | --- |
| H front porch | 0.942 µs | 64 |
| H sync | 1.88 µs | 128 |
| H back porch | 1.88 µs | 128 |
| H blanking | 4.71 µs | 320 = 64+128+128 |
| **H total** | 19.794 µs | **1344** |
| V front porch | 79.176 µs | 4 |
| V sync | 79.176 µs | 4 |
| V back porch | 673.0 µs | 34 |
| V blanking | 831 µs | 42 |
| **V total** | | **842** = 800 + 42 |

Both columns close on themselves, so the counts are exact integers rather than a
fit, and the oracle's 1346 and 841 were each off by one thing. Corrected.

**The dot clock stays 68 MHz, `PROVISIONAL`**, the same trade the monochrome
entry already carried: Table 11-4 implies 67.899 MHz and that does not divide
`AP_TIME_BASE_HZ` while 68 MHz does. Cost is 0.15% — 50.595 kHz against the
printed 50.519, 60.09 Hz against 60.0 — both inside Table 11-3's bounds. Closing
it means recomputing the time base, which changes the unit of account for every
clock in the machine and no behaviour.

### 19a: the monochrome total was right, and the note calling it a discrepancy was not

`ap_graphics.h` said Table 11-8's 8.47 ns pixel makes the line "1730 pixels
against `set_raw`'s 1728". Decomposed into the porches the table also prints —
407 ns front porch = 48, 1.49 µs sync = 176, 1.9 µs back porch = 224, so
blanking is 448 and the line 1280 + 448 — it is **1728 exactly**. The 2-pixel
gap was two roundings compounding, from dividing a printed sum by a printed
pixel time. The vertical closes the same way: 4 + 4 + 34 = 42 blanking lines and
1024 + 42 = 1066, which is what the code already had.

### Verification

`graphics_suite` and the full `ctest` stay green at 137/137. The reference
identity boot is **unaffected and its hash is still `A354786119A3931D`** — but
that proves only that nothing regressed, because that boot fits **no display**
(`identity-boot.sh` passes no `--screen`, and the census line reads `display
none`). The changed path is exercised by `./tools/identity-boot.sh --screen c8p`
instead, which is a different machine and a different hash by construction.

That run completes with the corrected raster and the controller genuinely
driven — **2,169,974 reads and 5,302,083 writes** to the display controller —
and hashes **`6140F8E43F3BCC1C`**. Recorded here with its invocation, because a
hash without one is not a reference; it is the first figure this project has
for a colour DN3500 and supersedes nothing.
