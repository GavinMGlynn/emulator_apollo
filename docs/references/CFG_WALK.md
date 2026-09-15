# `[CFG]` *HP-Apollo Products Configuration Guide* — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[CFG]` | `bitsavers/HP-Apollo_Products_Configuration_Guide_Dec89.pdf` | 383 | born-digital, moderate OCR damage | **IN PROGRESS** — the Series 3500 pages read, the other model sections owed |
| `[CFG]` | `bitsavers/5952-2149_Apollo_Quick-Reference_Configuration_Guide_Jul90.pdf` | 88 | born-digital | **owed** |

**December 1989, and the model table has been citing it since Phase 5 without
anyone reading it.** `ap_model.c` attributes memory ranges, displays, clock
speeds and option lists to `[CFG]` across most of its twelve rows;
`GRAPHICS.md` defines the tag. This is the first time the guide itself has been
opened, and it was opened because a *different* document — the SR10.4 release
notes — showed one of its figures to be stale.

## What it is, and what that makes it worth

A **sales configuration guide**: per-model description blocks, a functional
block diagram, a product summary listing what is in the box, and then pages of
ordering options. It is not a hardware manual and its numbers describe
*configured systems*, which is exactly the trap below.

## The DS5500 figure it could not have had

`018901-A00_WALK.md` has this: the model table gave the DN5500 a 32 MB ceiling
citing `[CFG]`, and the SR10.4 release notes of March 1992 give 64 MB. **This
guide is December 1989 and the DS5500 shipped in 1992**, so the figure was never
about that machine. Corrected there.

*The general lesson is on this row rather than that one*: `[CFG]` predates three
of the machines in the model table, and any field of theirs citing it is citing
a document that could not have known.

## The Series 3500's monitor, and a wrong correction avoided

The Series 3500 Monochrome Workstation's description block reads:

> "CPU: MC68030, clocked at 25 MHz. ... **Monitor: 19-inch, 1280 by 1024, 64-Hz
> Monochrome Monitor.** RAM: **4-MB or 8-MB** parity, expandable to **32-MB**.
> Built-in Interfaces: **3 RS-232C ports**, and monochrome graphics. ...
> Floating Point Processor: MC68882 clocked at 25 MHz, is standard."

and the product summary adds "**7 slot IBM PC AT/XT compatible bus (6 AT, 1
XT)**" and "**32-bit memory and Direct I/O buses**".

`ap_model.c` gives the DN3500 `AP_DISPLAY_MONO_1024X800`, and that field carried
**no citation** while every neighbour did. It looks wrong against the block
above. **It is right**, and the guide itself says why three pages later:

> `Opt. FM2` — "19\" monochrome graphics display, swivel and tilt. (**Requires
> option DM0**)"

DM0 is the *1280 by 1024 monochrome graphics controller*. So the resolution
comes from a **different controller**, not from the monitor's size — and
`008778-03` §11, a hardware manual walked whole, gives this board family's three
monitors as "15-inch colour 1024 x 800 at 60 Hz, 19-inch colour 1024 x 800 at 60
Hz", with §10.1's 4-plane controller at "1024 x 800 x 4".

**1024 x 800 is the board's resolution; 15 or 19 inches is a size; 1280 x 1024
needs DM0.** Which is exactly what the `DN3550` row already says — it is a
DN3500 board with DM0 and FM2 — and it cites both options where the DN3500 cited
nothing.

*The correction made is a citation, not a value.* An uncited field in the row
described as "reference superset" is what invites a plausible wrong change, and
this walk nearly made one. `ap_model.c` now carries the hardware manual's
figures, the guide's apparent contradiction, and the option that resolves it.

*Also captured from the same block, and cited*: "RAM: 4-MB or 8-MB parity" —
the row's comment said "8-32 MB supported", where the guide gives **two** base
sizes.

**And a fact this project already models without knowing the source**: "3
RS-232C ports". A DN3500 has two DUARTs and therefore four channels, and the run
report names `sio1 A` "the keyboard". Four channels, one of them the keyboard,
three RS-232C ports — the guide's number and this core's arrangement are the
same statement.

## Every model's description block, read as images (2026-09-15)

Each section's Description page (and, where the model table cites it, the
Product Summary) at 300 dpi, against `ap_model.c`:

| Page | Model | Against `ap_model.c` |
| --- | --- | --- |
| A-12 | the *HP-Apollo Workstation Specifications and Graphics Options* overview table | **the table is p. A-12, not A-11** -- p. A-11 is DSEE, DDE and Open Dialogue. Five citations in `ap_model.c`, one in `ap_board.c`, one in `model_suite.c` and the `--list-models` golden said A-11; all corrected. Its figures are as the row comments quote them: 2500 20 MHz 4-16 MB, 3500/3550 25 MHz 4-32 MB, 4500 `MC68030@30MHZ` 4-32 MB |
| D-4, D-5 | Series 2500 | 20 MHz 68030 and 68882, 4 MB expandable to 16, 15" 1024x800 or 19" 1280x1024 mono, 3 RS-232C on a break-out cable, SCSI; D-5's ordering line is the one the DN2500 row quotes, and Opt. DL0/DM0 are the two controllers. Agrees |
| D-13, D-19, D-26 | Model 3010A mono, colour, server | 12 MHz 68020 and 68881, 4 MB expandable to **8 MB, "system supports only one memory board"**, one RS-232C. The `DN3000` row's 12 MHz and 8 MB agree; the 3010A has no row |
| D-30 | Model 3040, rack-mounted | the same 3000 board, 15" 4-plane 1024x800 colour. No row |
| D-64 | Model 3540, rack-mounted | 25 MHz 68030 and 68882, 4 or 8 MB to 32, 3 RS-232. No row |
| D-77, D-86, D-96 | Model 3550 mono, colour, server | as the `DN3550` and `DSP3550` rows quote them, page for page; the colour model adds 15" 8-plane 1024x800 and 19" 8- or 40-plane 1280x1024, and D-77 says "can be upgraded to a Series 4500 by installing a CPU board upgrade kit" |
| D-103, D-104 | Series 4000 options | A-ADD-SWFC (SCSI/Winchester/floppy controller, "rev 25 (or greater) CPU"), 4 and 8 MB add-on boards, **A-ADD-FPA** the floating-point accelerator, which needs SR10.1 or later under SR10 |
| D-108, D-109, D-116, D-126 | Series 4500 mono, colour, server | 33 MHz 68030 and 68882, **8 or 16 MB base** to 32, 3 RS-232C, 19" 1280x1024 mono. **The ordering line the `DN4500` row quotes is p. D-109, not D-108**; D-108 is the Description, and its "clocked at 33 MHz" is a third statement against the overview's 30. Corrected. The row's "4-32 MB" is the overview's figure; the Description's base sizes are 8 and 16 |

**Two citation corrections and no value changes.** Every clock, part, memory
ceiling and display in the table agrees with the model's own page.

## What is owed

*The model description blocks were read next (above).* The Product Summary
option pages beyond those cited, the Series 10000 section, the upgrade matrices
(pp. 331-358), and the 88-page July 1990 quick reference. *This said "The Series
3000, 4000, 4500, 3010A and Model 3550 sections, the upgrade matrices (pp.
331-358), and the 88-page July 1990 quick reference."* **Every model section
should be read the way this one was**: description block, then options, then
against a hardware manual — because the description blocks describe configured
systems and will contradict the hardware on their own.
