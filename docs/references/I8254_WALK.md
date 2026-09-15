# `[8254]` walked whole

**State: 12/12 pages read as page images, 2026-09-15.** *1983 Intel
Microprocessors and Peripherals Handbook*, the 8254 chapter, pp. 6-150 to 6-161
(`docs/references/intel/1983_Intel_Microprocessors_and_Peripherals_Handbook.pdf`,
PDF pages 680-691). Cross-read against Intel's September 1993 datasheet,
order number 231164-005, pp. 8-12, fetched for the purpose.

**Why it was walked.** `ap_i8254` had been built from this chapter's register
figures and never checked against its mode definitions. Two booted Domain/OS
nodes counted no ring receives (`FINDINGS.md` C292), and the ring driver takes
its packet lengths from these counters with a rule -- a NULL COUNT branch --
that only makes sense if a written count reaches the counting element on the
next clock pulse, which the model did not do (`RING.md` 145c). One fact
missing means the document is read whole.

| PDF | Page | Section | Yield | Model before |
| --- | --- | --- | --- | --- |
| 680 | 6-150 | features, block diagram, pins | binary **or BCD** counting; three independent 16-bit counters | BCD not counted |
| 681 | 6-151 | Table 1 pin description, functional description | one CLK, GATE and OUT per counter | matches |
| 682 | 6-152 | read/write logic, control word register, Figure 5 counter internals | CE a "16-bit presettable synchronous down counter"; OL follows CE; "**CR_M and CR_L are cleared when the Counter is programmed**" | CR kept across a control word |
| 683 | 6-153 | Figure 7 control word; operational description | SC, RW, M, BCD fields; power-up state undefined | matches, BCD field unused |
| 684 | 6-154 | write operations, Figure 8, read operations | control word before count; interleaved programming allowed; a new count may be written at any time | matches |
| 685 | 6-155 | counter latch (Figure 9), read-back (Figure 10) | OL held until read or reprogrammed; repeated latches ignored | matches |
| 686 | 6-156 | status byte (Figure 11), NULL COUNT (Figure 12), Figure 13 | NULL COUNT 1 on control word and count writes (second byte of a pair), **0 only when CR -> CE** | cleared at the write |
| 687 | 6-157 | Figure 14; mode definitions; **mode 0**; Figure 15 | "**the initial count will be loaded on the next CLK pulse. This CLK pulse does not decrement the count**"; loaded even with GATE = 0; first byte of a pair disables counting and sets OUT low | loaded at the write; OUT untouched by the first byte |
| 688 | 6-158 | **mode 1** (Figure 16), **mode 2** (Figure 17) | one-shot loads on the CLK after a trigger; rate generator's OUT low when the count reaches **1**; a new count waits for the end of the cycle | mode 1 given mode 0's shape; mode 2 OUT low at 0 and never high again |
| 689 | 6-159 | mode 2 cont., **mode 3** (Figure 18), **mode 4** | square wave: even counts by two, odd counts load N-1 with OUT high (N+1)/2 and low (N-1)/2; strobe N+1 pulses after the write | odd counts wrong; mode 4 given mode 0's shape |
| 690 | 6-160 | Figures 19, 20, **mode 5**, Figure 21 gate summary | hardware strobe loads on the CLK after a trigger; GATE low sets OUT high at once in modes 2 and 3 | gate edge reloaded at once; OUT untouched |
| 691 | 6-161 | Figure 22 min/max counts; operation common to all modes | GATE sampled on the rising CLK edge, a trigger flip-flop in modes 1, 2, 3 and 5; loads and decrements on the falling edge; count 0 = 2^16 or 10^4; wrap to FFFF or 9999 in modes 0, 1, 4 and 5 | edge not latched; no BCD wrap |

**Derived into `src/core/device/ap_i8254.c`**: every row above. What stays as the
datasheet leaves it: "In mode 2, a COUNT of 1 is illegal" and Figure 22's
minimum of 2 for modes 2 and 3 are not policed -- the part's behaviour below
them is not stated, so none is invented; and BCD digits above 9 are not
defined either, and decode positionally.

*Verification*: `i8254_suite`, with a test per mode figure.
