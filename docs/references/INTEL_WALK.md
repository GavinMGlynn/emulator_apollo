# Intel peripheral datasheets — walk coverage record

The two parts the boot leans on hardest. Unlike the OMTI manuals these are
**small and have text layers**, so they are cheap to walk.

| Tag | File | Pages | Native | Text layer |
| --- | --- | --- | --- | --- |
| `[8237]` | `intel/8237A_DMA_Controller.pdf` | 19 | 301 ppi | **yes** |
| `[8259]` | `intel/8259A_231468-003_Dec1988.pdf` | 24 | 301 ppi | **yes** |
| — | `intel/1983_Intel_Microprocessors_and_Peripherals_Handbook.pdf` | 1031 | 600 ppi | yes — a fallback, likely contains both parts |

## STATUS: `[8237]` 1 of 19, `[8259]` **11 of 24 — the whole programming model**. Both confirm sentence by sentence; see the conclusion below.

Started 2026-08-21.

**Why these two.** The reference boot issues **401 disk commands** and 14.2 M
disk-register reads, and every one moves through DMA and interrupts. Unlike the
floppy — which Phase A measured as **never touched** by the boot — these parts
are on the path the machine actually exercises. Neither had a coverage record.

## Method

- Text layers exist, so extraction is usable **as a search** to find candidate
  pages. Read the page images for anything with a bit layout.
- 301 ppi is the native resolution; render at it, not below.

## Coverage — `[8237]`

| p. | Section | Yield | What it contained |
| --- | --- | --- | --- |
| 9 | Status register (cont.), Temporary register, **Software Commands**, **Figure 6 Software Command Codes** | `confirms` **— and the model quotes this page sentence by sentence** | Status bits 0-3 "set every time a TC is reached by that channel or an external EOP is applied ... **cleared upon Reset and on each Status Read**"; bits 4-7 "set whenever their corresponding channel is requesting service". **Both are in `ap_i8237.c` with those exact sentences as comments**, the request half computed live from `dreq & ~mask \| request` rather than latched. The three software commands: **Clear First/Last Flip-Flop**, **Master Clear** ("the same effect as the hardware Reset ... the Mask register is **set**"), **Clear Mask Register**. All three are modelled, Master Clear by calling `ap_i8237_reset`, which sets `mask = 0x0F` quoting "The entire register is also set by a Reset". Figure 6's full `A3`-`A0`/`IOR`/`IOW` decode, including the eight **Illegal** read combinations — which `PROJECT_STATUS` already cites for why a read of a write-only register returns zero here where the oracle returns `0F` |
| 7 | **REGISTER DESCRIPTION** — Current Address, Current Word, Base, Command, Mode, **Request**, Mask | **DEFECT FOUND** | Confirms: "the actual number of transfers will be one more than the number programmed"; "when the value in the register goes from zero to FFFFH, a TC will be generated"; "if it is not Autoinitialized, this register will have a count of FFFFH after TC"; "Autoinitialize takes place only after an EOP"; the base registers "cannot be read"; the mask bit set on EOP "**if the channel is not programmed for Autoinitialize**" — all present in `ap_i8237.c`, several quoted. **And one that was not**: "In order to make a software request, **the channel must be in Block Mode**." The encoder OR'd the request register in regardless of mode. Fixed, with the rule factored into one `asking_channels` because three sites combined the halves by hand. The document does not say what the part does when the rule is broken, so the oracle was consulted in order — `am9517a.cpp:205` gates on `MODE_BLOCK` |
| 5–6 | Transfer modes, TRANSFER TYPES, Memory-to-Memory, Autoinitialize, Priority, Compressed Timing, Address Generation | `confirms` **— including two rules that are easy to omit** | Single/Block/Demand/Cascade each as stated; **"the cascade channel ... does not output any address or control signals of its own"**, and `ap_i8237_transfer` returns for a cascade channel before doing anything; **"Verify transfers are pseudo transfers ... the memory and I/O control lines all remain inactive"**, modelled as a case that advances address and count and moves no byte, rather than an early return. Also confirmed: "the mask bit is not altered when the channel is in Autoinitialize", and channel 1's count being the one that ends a memory-to-memory service. Compressed timing, extended write and the `ADSTB` address multiplexing are pin-level and unmodelled |
| 8 | **Command, Mode, Request and Mask bit layouts** (unnumbered boxes) + **Figure 5, Definition of Register Codes** | `confirms` **— and corrects two of our citations** | Every bit of all four layouts matches `ap_i8237.h`. **Three don't-care conditions the prose omits**: command bit 1 "X If bit 0 = 0", bit 3 "X If bit 0 = 1", bit 5 "X If bit 3 = 1"; and the mode register's transfer type "XX If bits 6 and 7 = 11". Bit 1's is the one that mattered — the prose says channel 0 may hold its address "for **all transfers**", which reads as a defect in this core until the box shows the bit is meaningless unless memory-to-memory is on, which is exactly where `ap_i8237.c` consults it. **A near-miss recorded deliberately**: acting on the prose would have "fixed" correct code. *And the citations*: `ap_i8237.h` called both bit layouts "Figure 5", which is the register-codes table; Figure 6 is "Software Command Codes" and the layouts are unnumbered. The datasheet mis-cross-references them itself — its Command Register text says "See Figure 6 for address coding" where the coding is Figure 5 |
| 9–10 | SOFTWARE COMMANDS, Status and Temporary register text, **Figure 7 Word Count and Address Register Command Codes** | `confirms` **— the whole address/count decode** | Figure 7 read as an image: `A3` = 0, `A2`–`A0` giving channel × 2 plus 0 for address and 1 for count, which is `ap_i8237.c`'s `reg >> 1` and `reg & 1` exactly. **Write targets "Base and Current"** and **Read returns "Current"** — both as modelled, and the base registers "cannot be read" as the p. 7 text says. The flip-flop selects low byte at 0 and high at 1, toggling per access. Master Clear's five cleared registers — Command, Status, Request, Temporary, First/Last Flip-Flop — plus the mask set, all covered by `ap_i8237_reset`. **One thing the list omits**: the *Mode* registers, so the datasheet does not say what a reset leaves in them. This core zeroes them, which is inert (Verify moves no byte) and deterministic, and now says so |
| 2–4 | **Table 1, Pin Description** — CLK, CS, RESET, READY, HLDA, DREQ0–3, DB0–7, IOR, IOW, **EOP**, A0–A3, A4–A7, HRQ, DACK, AEN, ADSTB, MEMR, MEMW | `confirms` **— and two near-misses, both mine** | RESET's list matches p. 9's Master Clear exactly. **"Polarity of DREQ is programmable. Reset initializes these lines to active high"** — modelled, and modelled *in the right place*: `ap_i8237_dreq_level`/`ap_i8237_dack_level` report the level a board would measure, while the arbitration takes a logical request and is polarity-independent by construction. Both are exercised by `dma_suite` although nothing in `src/` calls them, and the header already says why. **`EOP` is bidirectional**, an external low terminating a service, which is modelled. **The near-miss**: "The mask bit and TC bit in the status word will be set ... unless the channel is programmed for Autoinitialize" reads as a contradiction of p. 7's "Bits 0–3 are set **every time** a TC is reached", and this core follows p. 7. The next sentence resolves it — "In that case, **the mask bit** remains unchanged" — so the exception is the mask bit's alone and the code is right. It looked like a contradiction only because an 18-line extraction cut the sentence off |
| — | Cross-printing check: the **1983 Intel handbook**'s copy of the same paragraph | `confirms`, with one wording difference | The handbook says "In that case, the mask bit remains **clear**" where the 1988 datasheet says "remains **unchanged**". The 1988 wording is the careful one — a mask bit already set stays set — and neither changes what this core does. Consulted as the *sibling printing* before any thought of the oracle, which is the step the resolution order puts second |
| 1, 11–19 | *(not read: AC/DC characteristics, waveforms, packaging)* | | |

## Coverage — `[8259]`

| p. | Section | Yield | What it contained |
| --- | --- | --- | --- |
| 15 | **OCW3** (ESMM/SMM), **Fully Nested Mode**, **End of Interrupt**, **AEOI**, **Automatic Rotation** | `confirms` **— including the page's subtlest sentence** | `ESMM`=1 with `SMM`=1 enters Special Mask Mode, `ESMM`=0 makes `SMM` "don't care": `ap_i8259.c` has `OCW3_SMM 0x20` and `OCW3_ESMM 0x40`. Fully nested: IR0 highest, IS set on acknowledge, "all further interrupts of the same or lower priority are inhibited" — the model walks levels from `highest_priority` and rotates. **Non-specific EOI "will automatically reset the highest IS bit of those that are set"** — quoted at `ap_i8259.c:223`. And the sentence this page turns on: **"an IS bit that is masked by an IMR bit will not be cleared by a non-specific EOI if the 8259A is in the Special Mask Mode"** — quoted at `ap_i8259.c:231`. That is the interaction a hand-written PIC gets wrong, and it is modelled. Also on the page and worth having: an EOI "must be issued **twice** if in the Cascade mode, once for the master and once for the corresponding slave"; AEOI is master-only on pre-1985 parts |
| 9 | **ICW1**'s six automatic effects (a–f), ICW2, **ICW3** | `confirms` **— all six, quoted a–f** | "The edge sense circuit is reset"; "The Interrupt Mask Register is cleared"; "IR7 input is assigned priority 7"; "The slave mode address is set to 7"; "Special Mask Mode is cleared and Status Read is set to IRR"; and IC4 = 0 zeroing every ICW4 function. `begin_initialization` implements **all six with the letters a–f as its own comments**, including the subtle one — the edge-sense reset is modelled by dropping `irr`, because "a line already high has no edge left to give". ICW3's two roles, master bitmap and slave ID, and the note "Slave ID is equal to the corresponding master IR input" |
| 10–11 | **ICW4** bit definitions, LTIM, ADI, SNGL, IC4 | `confirms` **— every bit position** | `SFNM` 4, `BUF` 3, `M/S` 2, `AEOI` 1, `µPM` 0 against `ap_i8259.c`'s `ICW4_SFNM 0x10`, `ICW4_BUF 0x08`, `ICW4_MS 0x04`, `ICW4_AEOI 0x02`, `ICW4_UPM 0x01` — exact. **And the clause a sweep would miss**: "If BUF = 0, M/S has no function." `master` is written and read by **nothing**, and `ap_i8259.h` already says "meaningful only when buffered" — so the bit reads back as a register bit must while acting on nothing, which is what the sentence requires. Checked rather than assumed, by sweeping the field's readers |
| 13–14 | **Figure 8**, OCW1/OCW2/OCW3 formats and definitions | `confirms` | OCW1's `M7`–`M0`, "M = 1 indicates the channel is masked"; OCW2's `R`/`SL`/`EOI` and `L2`–`L0`; OCW3's `ESMM`/`SMM`/`P`/`RR`/`RIS`. The A0 column matters and matches: OCW1 at A0 = 1, OCW2 and OCW3 at A0 = 0 |
| 16 | **Poll Command** | `confirms` **— and the model quotes it** | "The 8259A treats the next RD pulse ... as an interrupt acknowledge, **sets the appropriate IS bit if there is a request**, and reads the priority level", with the returned byte `I` plus `W2`–`W0`. `ap_i8259.c`'s read path does exactly that and quotes both sentences; `I = 0` with nothing to report returns zero |
| 17 | Interrupt Masks, Special Mask Mode, Reading the 8259A Status | `confirms` | Per-channel masking, "Masking an IR channel does not affect the other channels operation", and the ISR read needing `RR = 1, RIS = 1` before the RD |
| 17–18 | **Edge and Level Triggered Modes**, and the default IR7 | `confirms` **— including the sentence that distinguishes them** | "If LTIM = 0, an interrupt request will be recognized by a low to high transition ... The IR input can remain high without generating another interrupt", against LTIM = 1's level recognition — both modelled, and the header argues why edge mode must *not* re-latch from the pin. Then the passage this part turns on: an IR that goes low before the first INTA gives **"a DEFAULT IR7"**, and **"A normal IR7 interrupt will set the corresponding ISR bit, a default IR7 won't."** `ap_i8259_acknowledge_first` sets no ISR bit for the spurious level and says so in a comment that goes on to name the consequence — software EOIing a spurious interrupt corrupts a real one's nesting |
| 18–19 | **The Special Fully Nest Mode** (a and b), **Buffered Mode** | `confirms` | (a) a slave in service "is not locked out from the master's priority logic", which `resolve`'s cascade handling implements and comments; (b) the software protocol for exiting, which is the driver's and not the part's. Buffered mode's `SP/EN` is a pin this core has no wire for, and the two ICW4 bits that select it are stored |
| 1–8, 12, 15, 20–24 | *(not read: pin descriptions, timing/AC characteristics, packaging)* | | |

## Conclusion after one page of each: these parts were derived, not queried

**Both datasheets confirm, and the manner is the finding.** `ap_i8237.c` and
`ap_i8259.c` quote their manuals *sentence by sentence* — the status-read
clearing, the live request half, mask-set-on-reset, non-specific EOI taking the
highest IS bit, and the special-mask-mode exception to it. The last of those is
the subtlest thing on 8259A p. 15 and it is in the code with the sentence
attached.

**This record predicted exactly this** and said what to do about it: "if the
remaining register pages keep confirming, say so and move on rather than reading
on out of momentum." Both first pages confirmed, so that is the call.

**These two parts are not where a boot defect is.** That is worth as much as a
defect would have been, because it was the reason for choosing them: they are on
the path the boot exercises, and they check out at the level of the datasheet's
own wording.

**What would change this assessment**: a page carrying a *timing* figure rather
than a register field. Both walks so far have confirmed **behaviour**; neither
has tested a duration, and this project's last two real defects — the keyboard
buffer and the OMTI's 100 µs wait — were both timings that nothing modelled.
That is the shape to look for if these are picked up again.

## What one page already suggests

`ap_i8237.c` quotes this datasheet **sentence by sentence** — the status-read
clearing, the live request half, the mask-set-on-reset. That is the signature of
a part that was **derived from its own manual rather than queried**, which is the
opposite of what the OMTI walk found.

**Provisional expectation, to be confirmed or overturned by the remaining
pages**: `[8237]` will behave like `010005-00` and `019411-A00` — a document
already mined, where the walk buys provenance rather than defects. If pages 1-8
and 10-19 keep confirming, say so and move to `[8259]` rather than reading on
out of momentum.

## Resume here

**Next: `[8237]` p. 6-8 and 10-11**, which the section search shows carry the
Command, Mode, Request and Mask register layouts — the remaining bit tables.
Then pages 1-5 and 12-19, then `[8259]` whole.

**The `[8259]` question worth carrying in**: `ap_intr.h` already cites `[8259]`
for the cascade and for "as a master, the vector is the slave's". The parts of
that manual nobody has quoted are where a defect would be, and the boot's
interrupt path is exercised on every one of those 401 disk commands.


## Conclusion for `[8259]`, after eleven pages

**No defect.** Every register, every bit position and every stated behaviour
checked so far is implemented, and in most cases the model quotes the datasheet
sentence it comes from — ICW1's effects are literally lettered a–f in the code.
That is the signature of a part **derived from its own manual** rather than
queried, which is what the plan predicted for `[8237]` and expected to be less
true here.

The two findings are about *method*, not about the part:

1. **The one clause that could have hidden a defect was a negative.** "If
   BUF = 0, M/S has no function" is a statement that a bit must do nothing, and
   nothing in a register sweep or a green suite can distinguish "correctly
   inert" from "forgotten". It was settled by sweeping the field's **readers**
   and finding none — the same question that found the ring's unattached
   buffers, asked of a datasheet clause.
2. **The datasheet contradicts itself once**, and harmlessly: the EOI section
   says the IS bit resets automatically "when AEOI bit in **ICW1** is set", and
   the next section says "If AEOI = 1 in **ICW4**". ICW4 is correct — it is
   where Figure 7 puts the bit — and this core reads it there. Recorded so a
   later reader who meets the ICW1 sentence does not take it for a fact, which
   is the same service the OMTI `ST3` note performs.

*Remaining for a complete walk*: the pin descriptions and the AC/DC
characteristics, which describe wires and voltages this core has no model for,
and the packaging pages. Named rather than skipped silently.
