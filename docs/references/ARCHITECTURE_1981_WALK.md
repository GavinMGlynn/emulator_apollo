# `Apollo DOMAIN Architecture` (Feb 1981) — walk coverage record

| Tag | File | Pages | Text layer | State |
| --- | --- | --- | --- | --- |
| `[ARCH81]` | `bitsavers/Apollo_DOMAIN_Architecture_Feb81.pdf` | 31 | born-digital, heavy OCR damage | **WALKED WHOLE, 31/31, 2026-09-10** |
| `[ARCH81P]` | `bitsavers/Apollo_Domain_Architecture_Preliminary_Jan81.pdf` | 32 | born-digital, **very** heavy OCR damage | **WALKED WHOLE, 32/32, 2026-09-10** |

**The oldest document on the shelf and the densest.** `CONTENT_TRIAGE.md` ranked
it first by hardware terms per character — 31 hits in 46 K characters — and 
32 slides with a paragraph of commentary each, from the year before the first
Apollo shipped.

**Read as a witness, not a source.** Everything here is nine years older than the
DN3500 and six years older than `[MAC]`, so where it agrees with a walked manual
it is independent confirmation and where it disagrees it is a superseded design.
Both happen below.

## The ring's out-of-band symbols: same shape, different meanings

Slide II.3, RING NETWORK PROTOCOL, prints four nine-bit symbols:

```
TOKEN             = 011111100
FRAME             = 011111101
MESSAGE HEADER    = 011111110
MESSAGE SEPARATOR = 011111111
```

`ap_ring_mac.h` models `[MAC]` Figures 2-2 to 2-4 (1987):

```
0 111111 00   AP_RING_OOB_SEPARATOR
0 111111 01   AP_RING_OOB_FRAME_START
0 111111 10   AP_RING_OOB_FREE_TOKEN
0 111111 11   AP_RING_OOB_CLAIMED_TOKEN
```

**The shape is identical across six years** — a leading zero, six ones as the
deliberate bit-stuffing violation, then two type bits — which is an independent
confirmation of the encoding this core implements, from the design's own year.

**The assignments are permuted.** Only `…01` agrees. 1981's `…00` is the token
where 1987's is the separator; 1981's `…11` is the separator where 1987's is a
*claimed* token. And **1981 has one token where 1987 has two**: the free/claimed
distinction — how a node reserves the ring — did not exist yet.

*Nothing changes.* `[MAC]` is the specification this core implements and is nine
years closer to the hardware. **This is recorded because a reader meeting the
1981 table in a trace would decode every control symbol wrongly**, and because
the survival of the shape while the meanings moved is the kind of thing that
makes a nine-year-old paper worth its 31 pages.

## Three things it confirms

- **The display is `1024 x 800`**, slide II.6: a bit-map display of "1024 [x]
  800" moving between display memory and program memory at **32 Mbits/sec**.
  `008778-03` §11 gives the same resolution in 1987 and `CFG_WALK.md` records
  how a configuration guide made it look otherwise. **This is the oldest witness
  for it and it agrees.**
- **The object address is 96 bits because a UID is 64**, slides III.1 and III.2:
  "UNIQUE OBJECT NAMES (**64 BIT UIDs**)" and a name space diagram of "96 BIT
  ADDRESS, UNIQUE IN SPACE & TIME" decomposing as UID plus segment. `[AEGIS]`
  §10 calls object address space "96-bit" and never says why; **this does**.
- **The MMU translates the 68000's 24-bit space**, slide III.5 — which dates the
  paper precisely and explains why the reverse-mapped MMU of `[AEGIS]` §10.7.1
  exists at all.

## Two hardware details no walked document carries

**Slide III.6, MMU protection**, is a per-page scheme this project has not seen
elsewhere: four levels, `00 USER DOMAIN 0`, `01 USER DOMAIN 1`, `10 SPVR DOMAIN
0`, `11 SPVR DOMAIN 1`, each carrying EXECUTE, READ ACCESS and WRITE ACCESS
rights "**at this level and higher**", plus **per-page statistics**. A
ring-protection model rather than the two-level supervisor/user split the 68030
and 68040 give, and it belongs to the reverse-mapped hardware.

**Slide III.7, MMU I/O mapping**: "**WIRED PER DEVICE**", a MULTIBUS address of
a 16-bit word or byte, a 12-bit page and a 9-bit word through an **I/O map**.
This is the IOMAP that `[AEGIS]` §26.1.2 says a reverse-mapped PROM must enable
the MMU to reach, and the slide gives its shape.

**Neither applies to the machines this core models** — both are 68000-era
reverse-mapped hardware — and both are recorded because `PATENTS_WALK.md` and
`AEGIS_INTERNALS_WALK.md` both ended up needing somewhere to point if a
reverse-mapped node is ever modelled. This is that place.

## The January preliminary, and the one slide February removed

`Apollo_Domain_Architecture_Preliminary_Jan81.pdf` is the same deck a month
earlier, marked **COMPANY CONFIDENTIAL** where February's is not, slide for
slide in the same order — **with one exception**. January has a
`III.15 COMPILED OBJECT` between `III.14 COMPILATION/BINDING/EXECUTION` and
`IV.1 USER ENVIRONMENT OBJECTIVES`; February drops it, and everything after
shifts up by one. **That slide is the entire reason to read the preliminary.**

> "The compiled object format is comprised of two parts: The first major part is
> **position independent code and pure data** which is **directly mapped and
> executed** into a process address space. The second part is **a database used
> by the loader to create an impure temporary data object** which is
> subsequently **mapped into the impure part of a process address space**."

**That is `[ASM]` Appendix F's architecture, in one sentence, six years before
Appendix F.** §F.5.2: read-only sections are "represented by **memory image
format**" and "The loader **maps** read-only sections with read and execute-only
rights". §F.5.6: read/write sections are "represented in **template format**", a
series of records "used to initialize the read/write sections", which the loader
processes after zeroing. **Two parts, one mapped and one built — the same
design, stated as an intention in 1981 and as a byte layout in 1987.**

Its figure labels survive the OCR well enough to name the pieces:
`POSITION INDEPENDENT CODE`, `PURE DATA`, `MODULE DIRECTORY`, `SECTION TABLE`,
a relocation/external line, `HISTORY INFO`, `SYMBOL TABLES`, and
`PROCESS ADDRESS SPACE`. `[ASM]` Appendix F has the section index table, the
global table, the relocation records and the module information records under
their later names.

**And it dates position-independent code to the design.** `[DP]` chapter 2
presents PIC as what makes shared libraries possible — "inserting an extra level
of indirection for each external procedure and each reference to global data" —
and this slide has it in the object format from the beginning, which is why the
`A5`-relative transfer vector the DS5500 calls through is not a late addition
but the shape the system was drawn with.

*Why February dropped it is not stated and is not guessed at here.*

## Fidelity

*February's page count is `pdfinfo`'s 31, not the 32 a form-feed split reports* — the
third time this session that split has been one high, and `check_docs` has
caught every one. **Use `pdfinfo`.**

All 31 February slides and all 32 January ones read from the text layer.
February's is badly damaged — `DISPLAY`
arrives as `OrSPLA Y` and `PHYSICAL` as `PHYSICA~` — but the four ring symbols,
the four protection levels and the `1024 800` pair are digit strings that survive
intact and were each read twice. **The commentary paragraphs are prose and were
read; the slides are figures and their labels are only as good as the OCR**, so
no figure geometry is claimed here, only the values quoted above.

**January's is far worse** — `MEMORY MANAGEMENT UNIT` arrives as
`f1[:!J(lIlY IIAIiAGElifWT UIIIT` — and its *commentary paragraphs* survive where
its slide labels do not, which is the opposite of February's. That is why the
two were read together: **each edition is legible exactly where the other is
not**, and the `COMPILED OBJECT` quotation above is from January's commentary,
which is clean.
