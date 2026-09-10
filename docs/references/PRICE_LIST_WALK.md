# `Apollo Price List` (Jul 1988) — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[PL88]` | `bitsavers/Apollo_Price_List_Jul88.pdf` | 81 | born-digital | **model specification blocks read whole; the price tables triaged** |

**A selling document that turned out to be the shelf's most efficient
confirmation.** `CONTENT_TRIAGE.md`'s second pass found it holds 40 hard
hardware terms — 11 `MHz`, 7 `68020`, 7 `MC68020`, 5 `MC68030` — in **five
per-model specification blocks**.

## Five blocks, and four models this core implements

```
DN3000   1.5 MIPS   12 Mhz MC68020, 12 Mhz MC68881   4-8 MB
         MONO 15" 1024x800, 19" 1280x1024   COLOR 15"/19" 1024x800 4- or 8-plane
         155MB or 348MB ESDI, Floppy or Cartridge Tape, 7-slot IBM PC AT bus

DN3500   4 MIPS     25 Mhz MC68030, 25 Mhz MC68882   4-32 MB
         MONO 15" 1024x800, 19" 1280x1024
         COLOR 15"/19" 1024x800 4-/8-plane, 19" 1280x1024 8-plane 2-D accelerated

DN4000   4 MIPS     25 Mhz MC68020, 25 Mhz MC68881   4-32 MB
         MONO 19" 1280x1024

DN4500   7 MIPS     33 Mhz MC68030, 33 Mhz MC68882   64KB Physical Cache
         8-32 MB (two-way interleaved)   MONO 19" 1280x1024

DN5x0-T  20 Mhz MC68020, 20 Mhz MC68881, 16KB Physical Cache, FPA, 8-16 MB
```

**Everything checkable agrees with `ap_model.c`.** DN3000's `12000000`,
`AP_FPU_M68881`, `0x800000` and `MONO_1024X800`; DN3500's `25000000`,
`AP_FPU_M68882` and `0x2000000`; DN4500's `33000000`, `AP_FPU_M68882`,
`0x2000000` and `MONO_1280X1024`. **Nothing to change, from a source the table
does not cite.**

## And it settles the monitor question a third time

`CFG_WALK.md` untangles an apparent contradiction: `[CFG]`'s Series 3500 block
says "19-inch, 1280 by 1024" where `ap_model.c` says `1024x800`, resolved by
`Opt. FM2` requiring `Opt. DM0`, the 1280x1024 *controller*, and by `008778-03`
§11's monitors.

**This block prints both as alternatives on one line** — "MONO: **15", 1024 X
800   19", 1280 X 1024**" — which is the same statement without needing the
options list to decode it. Three sources now, and the DN3500's base is the
15-inch.

## Two DN4500 features the model table has no field for

> "DN4500 … **64KB Physical Cache** … MEMORY: 8-32 MB (**two-way
> interleaved**)"

Named here and nowhere else on the shelf. `ap_model.c`'s `has_virtual_cache` is
the **DS4000's** *virtual* cache, argued from `[S3K]`'s block diagrams because
no sentence names the models; a **physical** cache on a later board is a
different part in a different place, and the interleave is not modelled at all.

**Recorded on the DN4500 row as a named gap rather than added as a field**, with
the reason: both are *timing* features and this core's timing work is on the
DN3500, where the oracle is. A field with no behaviour behind it would be worse
than the absence.

*And both point at the same missing document.* `has_virtual_cache`'s own comment
says what would overturn it — "`007861-A01`, the DS3500's own handbook, which is
**unobtainable**" — and `RELEASE_NOTES_WALK.md` established that `007861` is
cited by order number and revision in **two** other Apollo manuals. The handbook
that would settle the DN4500's cache is the same one that would settle the
DS4000's, it demonstrably exists, and it is not public.

## What is owed

Nothing. The remaining pages are **prices and order numbers** — option
codes, country kits, upgrade part numbers — which carry no hardware fact the
specification blocks do not. *A price list is a selling document*, which is why
its figures above are recorded as confirmations of the model table and as a
named gap, and not as sources in their own right.
