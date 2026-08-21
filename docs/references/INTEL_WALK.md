# Intel peripheral datasheets — walk coverage record

The two parts the boot leans on hardest. Unlike the OMTI manuals these are
**small and have text layers**, so they are cheap to walk.

| Tag | File | Pages | Native | Text layer |
| --- | --- | --- | --- | --- |
| `[8237]` | `intel/8237A_DMA_Controller.pdf` | 19 | 301 ppi | **yes** |
| `[8259]` | `intel/8259A_231468-003_Dec1988.pdf` | 24 | 301 ppi | **yes** |
| — | `intel/1983_Intel_Microprocessors_and_Peripherals_Handbook.pdf` | 1031 | 600 ppi | yes — a fallback, likely contains both parts |

## STATUS: `[8237]` 1 of 19 pages walked. `[8259]` not started.

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
| 1–8, 10–19 | *(not yet read)* | | |

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
