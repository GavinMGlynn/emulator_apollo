# The patent shelf — triage record

`docs/references/bitsavers/patents/`, ten US patents assigned to Apollo Computer
Inc. **Nine had never been opened**; `SOFTWARE_SHELF_WALK.md` records how they
came to be unlisted.

**State: all ten triaged from their front pages, 2026-09-10. One is already a
primary source, one is a future source, eight are out of scope and say so.**

*Triage, not a walk.* Each patent's front page carries its title, assignee,
filing date, abstract and lead figure, which together settle **which machine it
describes** — and that is the only question worth asking before reading
twenty-odd pages of claims. The two in scope are marked for a full read when
something needs them; the eight out of scope are recorded so no one spends a
session discovering it again.

| Patent | Filed | Subject | Scope |
| --- | --- | --- | --- |
| **4,716,575** *Adaptively synchronized ring network* | — | the token ring | **IN USE** — `RING.md`'s `[PAT575]`, cited for figures `[MAC]` does not carry |
| **4,751,446** *Lookup table initialization* | Dec 1985 | a `256 × 3 × 8` colour LUT with three 8-bit DACs, "either eight plane or four plane display", loaded **through the display-memory data path** | **in the subject area** — see below |
| 4,857,901 *Display controller utilizing attribute bits* | Jul 1987 | a bitmap of **1280 × 1024 × 52 bits**, per-pixel interpretation-mode indexes through a `16 × 7` mode table, plane-routing logic, `2K × 24` colour tables | out — a high-end board, not the DN3000's 4- or 8-plane |
| 4,979,099 *Quasi-fair arbitration with default owner speedup* | Oct 1988 | a **decentralised, pipelined, synchronous** bus among Processor 1..N with atomic operations, lock assert/detect and arb-inhibit lines | out — a multiprocessor system bus, not the 68030's `BR`/`BG`/`BGACK` |
| 4,994,962 *Variable length cache fill* | Oct 1988 | fills of **16 or 64 bytes**, chosen by whether an **8-byte** transfer was requested, with **vector reference detection logic** | out — the 68030's line is 16 bytes and its burst is four long words |
| 5,022,004 *DRAM memory performance enhancement* | Oct 1988 | a **command queue**, RAS address compare, and page-mode or static-column cycles that skip precharge and RAS when the row repeats | out — a queued controller for the same machine |
| 4,746,773 *Connector for automatically maintaining…* | — | mechanical | out |
| 4,809,170 *Computer device for aiding in the development…* | — | a development tool | out |
| 4,951,192 *Device for managing software configurations* | — | DSEE | out |
| 5,023,907 *Network license server* | — | licensing | out |

## The three from October 1988 are one machine, and it is not a 68k node

`4,979,099` was filed 25 October 1988 and `4,994,962` and `5,022,004` three days
later, all three by Apollo. Read together they describe a **multiprocessor with
a vector unit, 64-byte cache lines, 8-byte transfers and a queued DRAM
controller**. That is the DN10000, and this core models the DN3000, DN3500,
DN4500 and DS5500 — single 68k processors with 16-byte cache lines and a
four-long-word burst.

**Recorded because the titles are exactly what a search for this project's open
items would surface.** `ap_arbiter` has open questions; so does the cache; so
does memory timing. All three patents *sound* like sources and none of them is,
and establishing that cost three front pages rather than three sessions.

## `4,751,446` is the one to keep

It describes a colour display whose lookup table is `256 × 3 × 8` bits feeding
three 8-bit DACs — the shape of the part `device/ap_bt458.c` models — and it
says "one data path can be utilized for either **eight plane or four plane**
display", which is the pair `board/ap_graphics.h` models.

**Its mechanism is one this core does not have.** The LUT is not written
directly by the processor: "Initialization data is stored in the display memory
and, during an initialization procedure, is applied to the lookup table **along
the same data path used by the addresses during display**." Two implementations
are claimed — a multiplexer that is a shift register with sequential pixel
addresses applied in parallel to interleaved stages, shifting disabled during
initialisation; or a buffer between the LUT's address and data inputs, filled on
alternate cycles of the display memory output.

**Not acted on, and the reason is that it names no model.** Filed December 1985,
where the BT458-based boards are later, and `ap_bt458.h` derives its behaviour
from Brooktree's own datasheet — an MPU address register with auto-incrementing
colour bytes — which is what a *part* datasheet says and is not in conflict with
a *board* that drives that part from display memory. **If the lookup table's
initialisation path ever becomes a question** — a firmware write sequence that
does not match the datasheet, a LUT that will not load — this is where to look,
and it should then be read whole rather than triaged.

## What triage does not claim

That any of these contains nothing. A front page settles the machine and the
subject; it does not settle whether claim 7 of an out-of-scope patent mentions a
figure this project wants. **Eight are marked out of scope on their subject, and
that judgement is on the record here so it can be overturned by anyone who finds
a reason.**
