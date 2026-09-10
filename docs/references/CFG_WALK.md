# `[CFG]` *HP-Apollo Products Configuration Guide* — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[CFG]` | `bitsavers/HP-Apollo_Products_Configuration_Guide_Dec89.pdf` | 384 | born-digital, moderate OCR damage | **IN PROGRESS** — the Series 3500 pages read, the other model sections owed |
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

## What is owed

The Series 3000, 4000, 4500, 3010A and Model 3550 sections, the upgrade matrices
(pp. 331-358), and the 88-page July 1990 quick reference. **Every model section
should be read the way this one was**: description block, then options, then
against a hardware manual — because the description blocks describe configured
systems and will contradict the hardware on their own.
