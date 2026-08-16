# The test shelf: content, and the subsystem each piece stresses

Phase 9 is content testing — running real Domain/OS rather than more unit tests,
because content finds what tests do not. This file is the shelf: what we hold,
what each piece exercises, and what we do not hold and would need.

**Every row is marked.** `observed` means this project has actually run it and
`PROJECT_STATUS.md` records the result. `expected` means it is reasoned from the
subsystem and has **not** been run here — it is a plan, not a finding, and must
not be cited as one. The distinction is the whole value of the file: a shelf that
mixes the two is a list of things someone once assumed.

## What we hold

Neither the volume nor the cartridges are in this repository, and they will not
be — see `docs/references/DOMAINOS_IMAGE.md`, which pins all six by SHA-256.

| Item | What it is |
| --- | --- |
| `media/dn3500-sr10.4-installed.awd` | Domain/OS SR10.4, 'large' template, installed and cleanly shut down. The boot every measurement in Phase 8 uses |
| `media/dn3500-osclean.awd` | OS restored from the boot cartridge, MINST not run. The right base for redoing an install |
| five `.ct` cartridges | The SR10.4 distribution, `019593-001` and `019594-001`..`004` |
| `roms/firmware/*_BOOT_*.bin` | Six boot PROMs across five models — see the firmware sweep in `PROJECT_STATUS.md` |

## By subsystem

### Reached by simply booting — `observed`

The 350 M-instruction reference boot (`tools/identity-boot.sh`) already exercises
these, and its report counts every one. They need no extra content; what they
need is a *longer* boot, which is what Phase 8's speed work is for.

| Subsystem | What the boot does to it | Evidence in the report |
| --- | --- | --- |
| Winchester disk, OMTI controller | 1275 commands, 1.34 M register accesses | `disk commands`, `disk reg N` |
| Serial, both MC68681s | 35 M reads, autobaud, the console dialogue | `regions`, `sio1 reg N` |
| MMU, ATC, table search | 56,688 descriptor fetches, 15,534 history updates | `atc fills` |
| Interrupts, both 8259s | 392 vector-2 exceptions and four others | `exceptions` |
| DMA, both 8237s | 2 transfers, 1.46 G bus ticks | `dma`, `dma bus` |
| Calendar | 58 update cycles | `calendar` |
| AT bus, empty slots | 7.4 M reads answered `FF` | `empty slot` |
| Parity | 4 forced errors, self-test 7 | `parity` |
| Boot PROM | 1.33 M reads | `regions` |

### Reached only with more content — `expected`

| Subsystem | What would stress it | Why it is not yet run |
| --- | --- | --- |
| Graphics, blitter, colour map | Any windowed session; the Display Manager draws continuously | The boot reaches the framebuffer but the crash at `00120020` precedes a session |
| Apollo Token Ring | `lcnode` on two nodes | Needs the ring controller device, which is blocked on register meanings — `RING.md` |
| Cartridge tape | A `wbak`/`rbak` to tape | Nothing has driven the SC499 past the firmware's own probe |
| Floppy | Reading a real `.afd` under the OS | The reader is tested standalone (`--floppy`); the OS path is not |
| FPU, 68882 | Any floating-point application | The part is modelled and unit-tested; no content has run through it |
| Keyboard, mouse | An interactive session | Headless by design; the SDL frontend is deliberately not stubbed |

### Attested commands

Reached and recorded by this project, so they can be relied on as entry points:

| Command | Prompt | What it reaches | Recorded |
| --- | --- | --- | --- |
| `ex domain_os` | `>` (MD) | Loads the OS from a volume | `MD.md` |
| `ex config` | `>` (MD) | The calendar's configuration table | `PROJECT_STATUS.md` |
| `shut` | `)` (bootshell) | Clean shutdown before copying an image | `DOMAINOS_IMAGE.md` |
| Ctrl-D | `$` (Aegis shell) | Drops back to the bootshell | `DOMAINOS_IMAGE.md` |

Anything else — `lcnode`, `wbak`, `netstat`, the Display Manager's own commands —
is `expected` and belongs in the table above until a run here records it.

## What we do not hold

Named so that the gap is visible rather than implied.

- **SR9.7, SR10.1, SR10.2.** The plan wants every obtainable release booted.
  **SR10.3 is now held** -- five cartridges in `media/sr10.3/`, plus
  `Apollo_DOMAINOS_SR10.3.5.tgz` -- and `sau7/config` and `sau7/self_test` have
  been read out of the boot cartridge with `tools/ct_extract.py`. What is *not*
  held for it is an installed volume: booting a release means installing it
  first, and the SR10.4 install ran under the oracle over a session.
  **Booting a release directly from its boot cartridge does not work yet, and
  the blocker is now located to the instruction.**
  `00002BE0` is **not** a hang: `subq.l #$1,d2 / bgt` is the disk controller's
  reset-settle delay, `$A00000` iterations armed at `0002BDA` with
  `a0 = 0004D000`. Given 500 M instructions instead of 150 M it completes, the
  PROM prints `Disk 04 03FEFF 00 W` and `Could not load /SAU7/SELF_TEST.`, and
  falls through to the **console-selection poll at `00078E`** -- which is where
  it now sits, at `0007A2`/`0007A8`.
  That poll offers three consoles, each a status bit 0 and each posting its own
  code: `$2(a0)` the keyboard (`00080E`, posts `09`), `$12(a0)` **serial 1
  channel B** (`0007E6`, posts `0A`, then `adda.l #$10,a0`), and `$102(a0)`
  serial 2 (`0007B0`, posts `0B`, then `adda.l #$100,a0`). None is ever
  satisfied -- no `09`/`0A`/`0B` appears in the posted codes -- so MD's banner
  never prints and `di c` cannot be issued.
  **The characters do arrive, and the loop is an autobaud.** `--boot-report`
  now covers the channel a run types at rather than only the keyboard, and it
  shows serial 1 channel B going from `3 read(s)` to `11 read(s)` when ten
  spaced carriage returns are sent -- every one taken by the firmware -- while
  `RxRDY` stays clear and no `09`/`0A`/`0B` is posted. The tail of the loop is
  why: `000822`-`00084A` writes `$12(a0,d4.w)` from `$159(a6)`, compares `d1`
  against `$FF` and writes `#$BB`, which on the MC68681 is **CSR, the clock
  select** -- the PROM is trying baud rates against each character and
  discarding the ones it cannot read. So the blocker is not delivery but
  **speaking its language**. The table is at `000844`-`0008B8` and is fully
  legible: the byte read is matched against `$FF` -> clock select `$BB`, `$FE`
  -> `$99`, `$C7` -> `$88`, `$72` -> `$66` and `$C0` -> `$44`. Those five are
  the shapes a carriage return takes when sampled at the *wrong* rate, and none
  of them is printable -- so a console script that could only write `\r` could
  only ever send `$0D`, which matches nothing. `send` now takes `\xHH`, and
  with it **the autobaud fires**: the firmware reads `$FE`, writes `$99`, and
  the channel's CSR changes from `77` to `99` for the first time.
  Two characters are needed before MD exists and `0007F0` is why -- the first
  is spent in the table above, and only the second, with bit 0 of `$158(a6)`
  now set, reaches the console-selected path at `0007F8`. **What is still open, and it is the harness**:
  `0007E6` is reached **exactly once** and `0007F8` (console selected) never,
  because the sender's rate is **fixed for a whole run** while the autobaud
  changes the *receiver's* rate mid-run. `ap_mc68681`'s `rate_matches` does not
  drop a mismatched character, it **corrupts** it -- faithful UART behaviour,
  and exactly what the table decodes -- so `$FF` at the default sender `0xBB`
  into a receiver at `77` reads as `$FE` and fires the `$99` entry. After that
  the receiver is at `99` and the sender is still at `BB`, so nothing further
  arrives intact. Measured the other way round too: with the sender at `0x99`, a
  carriage return into a receiver still at `77` reads as `$F9`, which no entry
  matches. Closing it means one of two things, and the second looks
  likelier. Either the scripted terminal changes rate when the receiver does --
  a harness change -- or **`ap_mc68681`'s misread is not the one the table
  inverts**. The five table entries are the *specific* bytes a carriage return
  resamples to at each rate ratio, so the firmware can recover the sender's rate
  from what it read: sender `BB` into receiver `77` ought therefore to read
  `$FF`, the entry that selects `BB`, and convergence would be automatic for a
  plain `\r`. Ours reads `$FE` and selects `$99`, which converges on the wrong
  rate. **CORRECTION: `ap_mc68681_resample` exists and is
  right.** The claim that nothing resamples the bit stream was wrong -- it was
  made from `receive_at`'s framing-error line without reading the twenty lines
  above it. The function walks each data bit to where the *receiver* believes it
  sits, in sender bit times, and reads stop/idle as high; worked by hand for
  sender 9600 into receiver 1050 every sample lands at or past position 13, so
  all eight bits read idle and the byte is **`$FF`** -- exactly the entry that
  selects `$BB`, the sender's own rate. So the model does invert the table
  correctly and a plain `\r` should converge.
  **What is unexplained is the observed `$FE`**, one bit off that, when `$FF`
  was sent at the default `0xBB` into a receiver reported at `77`. **It is `ACR[7]`, the baud-set select.** Both
  delivery sites in `main.c` pass `--boot-input-rate` correctly, so the swap is
  inside `receive_at`, which resolves *both* CSRs through
  `ap_mc68681_baud(code, acr_set_two)`. The two published sets differ at five
  codes and **code `7` is one of them -- 1050 in set 1, 2000 in set 2** -- and
  `CSRB = 77` is code 7 on both halves. `resample(0x0D, 8, 9600, 2000)` is
  `$FE`, which is exactly what the boots read.
  So the convergence question is entirely `ACR[7]`: **clear** puts the receiver
  at 1050, a `0D` from a 9600 terminal resamples to `$FF`, and the table maps
  `$FF` to `$BB` -- the sender's own rate, so the link agrees on the second
  character and MD opens. **Set** puts it at 2000, the same `0D` gives `$FE`,
  the table maps that to `$99` = 4800, which the sender is not, and the poll
  never ends. `receive_at`'s comment claimed the sets agree on every code this
  firmware uses; they do not, and `ap_mc68681_baud` eighty lines above said so
  all along. Corrected in place.
  **MEASURED, and it refutes the paragraph above as a *cause*: `ACR` is `60`,
  bit 7 clear, baud set 1** -- on both channels, so the receiver resolves code
  `7` to 1050 exactly as it should. The comment that was corrected was really
  wrong and the two sets really do differ at code 7, but that is not what the
  cartridge boots hit. `--boot-report` now prints `ACR`, the set and the
  resolved rate, so this costs one line instead of an argument.
  **And the `$FE` that started this was not evidence.** It was read from a
  *final register dump* at the 500 M limit, not from a stop-PC at the read
  instruction -- `d1` at the end of a run is whatever `d1` last held, and this
  project has a rule about exactly that (`dump-is-not-evidence-about-earlier-
  code`). Only the `$F9` came from a real stop-PC. Two rounds of rate reasoning
  were built on the unreliable one.
  The plumbing itself is verified **correct** by code read: `boot_input_rate`
  defaults to `0xBB`, is parsed and passed as-is, and `receive_at` takes the
  sender's rate from the *low* nibble and the receiver's from the *high* nibble
  under the same `ACR` set. Nothing there is swapped.
  **The measurement, finally taken properly: `d1 = $FE`**, stop-PC on `0007F0`
  at the *default* sender rate `0xBB`. Working it backwards, `0x0D` from a 9600
  sender reads `$FE` only if the receiver is at **2000 baud** -- `p0 = 14400/R`
  must land on one of `0x0D`'s zero bits while `p1 = 24000/R` clears the stop
  bit, which holds at `R = 2000`. **2000 is code `7` in set *2*.** `CSRB` is
  `77`, so the receiver is being resolved with the wrong baud set.
  **So `ACR[7]` is implicated after all, and the refutation above was made from
  the wrong run**: that `ACR 60` came from a *different* boot -- no cartridge,
  8 M instructions, `CSRB` still `BB` -- not from this one at 164 M. Same error
  as the register dump, one level up: evidence taken from a context that was not
  the one in question. **Next: print `ACR` from the cartridge run at the moment
  of the read**, and if it is set, find what sets it.
  The DEV BIT ARRAY is **not** what gates the tape: setting bit 1 `ctape`
  changes nothing, measured. The PROM does carry `Cartridge Tape  ` and
  `Ctape ERROR, SENSE BYTES = `, so the device is supported once selected.
- **Applications.** No compilers, no DSEE, no networking suites. The
  distribution cartridges we hold are the standard software bundle.
- **Release notes.** `docs/references/bitsavers/release_notes/` and `SR10/` exist
  and are empty.

## How to use this file

Take a row marked `expected`, run it, and either move it to `observed` with its
evidence or record why it could not run. A row that moves takes its detail to
`PROJECT_STATUS.md` and leaves a pointer here — the same rule the plan follows.

## The distribution media exists, and it is in the format this core reads

`bitsavers.org/bits/Apollo/` -- the *bits* tree, not the PDF one -- carries
Apollo cartridge tape images, and `Apollo_JRJ/readme-jrj.txt` gives the format:
**cptape, 512-byte blocks**, verified by Jay Jaeger against multiple drives.
That is `ap_ct`'s format exactly, which `ap_ct_open` states as "a whole number
of 512-byte blocks" (finding C24).

| Where | What | For |
| --- | --- | --- |
| `Apollo_JRJ/SR10.4/` | five `.ct.gz`: `CRTG_STD_SFW_BOOT_1` plus `STD_SFW_1`-`4`, 13-17 MB each | the install set for the release this project already runs |
| `Apollo_JRJ/SR10.3/`, `SR10.2/`, `SR10.4.1/` | the same shape | **releases this project does not hold** |
| `SR10.3/Apollo_DOMAINOS_SR10.3.5.tgz` | 67 MB archive | a second route to SR10.3 |
| `Apollo_JRJ/A_ADD_ETH/` | `009886.CRTG_A_ADD_ETH-A_ADD_ETH_V2.0-Aegis9.5-REV.01.ct.gz`, 243 KB | the Ethernet product cartridge |
| `Apollo_JRJ/` others | `NFS`, `MOTIF`, `HPVUE`, `DSEE`, `CC`, `FTN`, dated `M68K_*` builds | content for Phase 9 |

**Verified on a real one rather than assumed.** The Ethernet cartridge
decompresses to 805,888 bytes -- 1574 blocks exactly -- and reads end to end
through this core's own reader:

```
cartridge .../eth.ct
  bytes        805888
  blocks       1574
  blocks read  1574 (all)
  boot record  none -- a data cartridge
```

So the media path built this month is not speculative: it accepts a genuine
Apollo distribution tape, and correctly calls this one a data cartridge rather
than a bootable set.

**And it may short-circuit the AEGIS walk.** `RING.md` 85-85d is extracting
`ring8a.drvr` from the installed volume's hashed VTOC, which is four layers deep
and one field short. The distribution tapes carry the same driver in a
*sequential* archive, which is a different and probably smaller problem -- worth
trying before the last VTOCE offset is chased.

### SR10.3 is now held

Downloaded to `media/sr10.3/`: the five `Std_Sfw` cartridges, 236 MB
uncompressed. Every one is a whole number of 512-byte blocks and every one
reads end to end through `--tape`; the boot cartridge reports a real boot
record, `load 0013D800 entry 0013D82A length 7968`.

That is the first release beyond SR10.4 this project holds, and it moves "Boot
every Domain/OS release obtainable" from blocked-on-media to blocked-on-nothing
for one more release. `media/` is gitignored, so the files are named here rather
than committed.

