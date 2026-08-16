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
  now set, reaches the console-selected path at `0007F8`. **What is still open**
  is the handshake after the rate changes: the harness goes on sending at the
  old setting, so the character that should select the console is not read as
  one.
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

