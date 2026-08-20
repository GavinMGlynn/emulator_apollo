/* Apollo core-board registers: `008778-03` Table 2-8, behaviour measured.
 *
 * ## Why this module cites a probe and not a page
 *
 * Table 2-8 gives each of these registers an address and a name, and says
 * nothing whatever about its bits. The manual that carries the layouts is the
 * *Domain Personal Workstations and Servers Hardware Architecture Handbook*;
 * `docs/references/` holds only `019411-A00`, an addendum that patches its
 * Chapter 4. There is no paper route.
 *
 * So the behaviour here was measured against the oracle with
 * `tools/mame-oracle/regprobe.lua`, which drives every bit in both directions
 * and records the read-back. `FINDINGS.md` C10 has the method, the control
 * experiment and the results. Every figure in this file traces there.
 *
 * ## What is modelled, and the line this module will not cross
 *
 * The measurement settles **width, aliasing, and which bits are storage**. It
 * settles *nothing* about what any bit means.
 *
 * That is enough to implement, and it is deliberately not enough to build on. A
 * register whose sixteen bits all store can be read and written correctly by
 * firmware without anyone knowing what the firmware meant by them. What must
 * not happen is for some other subsystem to start depending on, say, bit 2 of
 * the control register enabling something -- because no source here says it
 * does. If a device ever needs one of these bits to mean something, that
 * meaning has to be established first, not assumed from a name in Table 2-8.
 *
 * ## The two registers Table 2-8 lists and the oracle does not have
 *
 * Task alias (`010300`) and master request (`011600`) are **modelled as
 * storage**: byte-wide, writable, reading back what was written. They were
 * declined for a long time, on the grounds that the oracle answers all-ones at
 * both and that copying that would be importing an oracle gap wearing the
 * clothes of a measurement. That argument was against inventing a *value*, and
 * it still holds -- what it does not justify is having no register at all where
 * Table 2-8 says the hardware has one, and where the firmware writes one 29
 * times.
 *
 * So the storage exists and the *meaning* is still not invented. `008778-03`
 * §2.4.7 gives the master request register a function -- "By setting a
 * particular bit in this register, an external processor asserts its DMA
 * Request signal to the system processor" -- and pointedly does not say which
 * bit. Nothing here acts on any bit, and nothing should until a source names
 * one; `ap_boardreg_master_request` exposes the byte so that an external-master
 * model can be built on evidence rather than on a guess made here first.
 *
 * `ap_boardreg_is_declined` is kept and now answers false for both: a caller
 * that asked "is this a register you refuse to model" gets a truthful no.
 *
 * ## What the firmware says about them, which is more than the oracle does
 *
 * Every boot PROM in hand was scanned for absolute references to both
 * addresses. The master request register is referenced **29 times across three
 * images** -- nine in `3500_BOOT_12191_7`, nine in `4500_BOOT_13167_02`, eleven
 * in `5500_BOOT_A1631-80046` -- and **not once** in either Series 3000 image or
 * in the DN2500's. That matches `008778-03` §2.4.7 exactly: "In the Series
 * 4000, an alternate method of bus arbitration exists that implements a Master
 * Request Register." It also puts the DS3500 in the Series 4000 architecture
 * group, which is the same set `019411-A00` §4.2.1.4 gives the address
 * translation map to -- DS3500, DS4000, DS4500, DS5500. Two features, one
 * model set, from two independent sources.
 *
 * Every one of those 29 sites is a **byte write**: `CLR.B`, or `MOVE.B` of
 * `$00`, `$02`, `$08` or `$40`. So the register is byte-wide, and the firmware
 * drives bits 1, 3 and 6. Two of the sites are the arms of one branch -- `$08`
 * on one path and `$40` on the other -- so at least those two bits are
 * alternatives rather than a sequence.
 *
 * **Not one site reads it**, in any image. That is the finding that matters,
 * because the read-back value is precisely what could not be measured: no
 * firmware in hand depends on it, so declining the read costs nothing that any
 * software here would notice. A 400,000-instruction DS3500 boot confirms it
 * from the other direction -- one write, no reads.
 *
 * **Task alias is at no absolute address in any of the five images**, and the
 * same boot never touches it. So there is nothing to disassemble: this one
 * needs the architecture handbook and only the handbook.
 *
 * None of this says what a bit *means*, and nothing may be built as though it
 * did. Width, use, and the absence of a read are what the firmware can testify
 * to.
 */

#ifndef APOLLO_BOARD_AP_BOARDREG_H
#define APOLLO_BOARD_AP_BOARDREG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AP_BOARDREG_CPU_STATUS = 0,
  AP_BOARDREG_CPU_CONTROL,
  AP_BOARDREG_CACHE_CONTROL,
  AP_BOARDREG_LATCH_PAGE_ON_PARITY,
  AP_BOARDREG_SELECTIVE_CLEAR,
  /* Both of Table 2-8's remaining registers. They *exist* and are byte-wide and
   * writable; what no source gives is what their bits mean. See the header --
   * the storage is modelled, the semantics are not invented. */
  AP_BOARDREG_MASTER_REQUEST,
  AP_BOARDREG_TASK_ALIAS,
  /* The one register in this file with a published bit layout *and* a published
   * value for every configuration it can hold. DS5500 only; see below. */
  AP_BOARDREG_MEMORY_PRESENT,
  AP_BOARDREG_COUNT,
} ap_boardreg_id_t;

/* Table 2-8 gives each register a 256-byte range. Measured: within a range the
 * register is aliased -- `010201` behaves identically to `010200` -- so the
 * range is the decode and the low byte of the address is ignored. */
#define AP_BOARDREG_RANGE 0x100u

#define AP_BOARDREG_CPU_STATUS_ADDR 0x010000u
#define AP_BOARDREG_CPU_CONTROL_ADDR 0x010100u
#define AP_BOARDREG_CACHE_CONTROL_ADDR 0x010200u
#define AP_BOARDREG_TASK_ALIAS_ADDR 0x010300u
/* ## The parity error register, and what it actually contains
 *
 * `008778-03` Table 2-8 gives this only a name -- "Latch Page on Parity" -- and
 * this file modelled it as sixteen bits of storage on that basis. `002398-04`
 * p. 12-27 gives its contents, for the DN3000 where it sits at `9300`:
 *
 *     PARITY ERROR REGISTER (read only)  [ 9300 | 03FFA300 ]
 *
 *     15 14 13 ......................... 0
 *     | x | x |        FAILING PPN        |
 *
 *     The upper two bits must be masked off.
 *
 * -- cited to `type mmu_$parity_ppn in /os/kins/term.pvt.pas`. So it is the
 * **failing physical page number**, fourteen bits, and it is **read only**.
 *
 * Both halves matter and this core had one of them. `ap_board.c`'s
 * `parity_error` already latches `address >> 10`, which is the page number on a
 * machine whose page is 1024 bytes -- and fourteen bits is exactly what the
 * DN3000's 24-bit physical address leaves after that shift, so the field width
 * confirms the shift and the page size rather than merely permitting them. What
 * was wrong is that a bus write **stored** here, so a program could overwrite
 * the one thing a parity handler reads.
 *
 * The fourteen bits are **not** enforced. They are the DN3000's address space,
 * and this core models a machine with a wider one: `019411-A00` §4.2.1.4 gives
 * the Series 4000's physical page number as bits `<25:10>`, sixteen of them, so
 * masking to fourteen would throw away real address bits. The two documents
 * describe the same field on two machines. */
#define AP_BOARDREG_LATCH_PAGE_ADDR 0x011300u
#define AP_BOARDREG_MASTER_REQUEST_ADDR 0x011600u
#define AP_BOARDREG_SELECTIVE_CLEAR_ADDR 0x016400u
/* `019411-A00` Table 2-5, which replaces page 2-7: `011400`-`0114FF`. Absent
 * from `008778-03` Table 2-8 entirely, which is why only the DS5500 map places
 * it -- see `ap_board.c`. */
#define AP_BOARDREG_MEMORY_PRESENT_ADDR 0x011400u

/* ## The memory present register, and the only complete bit table in this file
 *
 * `019411-A00` §4.2.1.18, added after §4.2.1.17: "This 8-bit, read-only
 * register exists in the DS5500. This register holds memory board existence
 * information." Every other register here was measured because no page carried
 * its bits; this one has a page, a figure, *and* a table of the register's value
 * for all 35 configurations it can hold. Nothing in it is inferred.
 *
 *     MEM Present <7-0>
 *     These bits are cleared (0) when memory boards are present.
 *
 * "In this register, each consecutive pair of bits identifies the condition of a
 * memory board slot. Bits 0 and 1 are slot 0, bits 2 and 3 are slot 1, bits 4
 * and 5 are slot 2, and bits 6 and 7 are slot 3."
 *
 * ## The two-bit code, which the manual gives only as a table
 *
 * §4.2.1.18 says a pair identifies a slot's *condition* and never says what the
 * four values mean. The table on the following page does, exhaustively -- it
 * lists the register value for every combination of 4, 8 and 16 MB boards and
 * empty slots -- so the code is read back out of it rather than guessed:
 *
 *     11  no board      (No Board) reads FF, every pair set
 *     10  4 MB          `4 - - -` reads FE: slot 0's pair is 10
 *     00  8 MB          `8 - - -` reads FC: slot 0's pair is 00
 *     01  16 MB         `16 - - -` reads FD: slot 0's pair is 01
 *
 * Checked against the far end of the table rather than only its head, because a
 * code recovered from four rows and applied to thirty-five is exactly the kind
 * of reading that fits the easy cases: `16 8 8 4` reads `81` -- `10 00 00 01`,
 * slot 3 a 4 MB and slot 0 a 16 MB -- and `16 16 16 16` reads `55`, four `01`
 * pairs. `test_the_memory_present_register_matches_every_published_configuration`
 * asserts all thirty-five rows, which is the whole table and not a sample.
 *
 * Note the code is not ordered by size: 8 MB is `00` and 4 MB is `10`. A model
 * that sorted the codes by capacity would agree with the manual on `8 8 8 8`
 * and disagree on most of the rest.
 *
 * ## Which slot is which board
 *
 * "Slot 0 is location P25 on the CPU Motherboard (Right-most slot)", then P24,
 * P23, and "Slot 3 is location P22 on the CPU Motherboard (Left-most slot)".
 * Recorded because it is the fact a service manual would want and this is the
 * only source on disk that carries it; nothing here depends on the geometry.
 *
 * ## Why it agrees with the calendar's configuration table by construction
 *
 * Domain/OS SELF_TEST prints, per slot, both "megabytes of memory in
 * configuration table" and "megabytes of memory sized" and complains when they
 * differ -- the battery RAM is the first and this register is the second. So
 * the split from a total to four boards is `ap_calendar_set_memory_boards`'s
 * and this file does not repeat it: the board hands both the same per-slot
 * sizes. See `ap_calendar.h`. */
#define AP_BOARDREG_MEM_PRESENT_SLOTS 4u
#define AP_BOARDREG_MEM_PRESENT_CODE_8MB 0u
#define AP_BOARDREG_MEM_PRESENT_CODE_16MB 1u
#define AP_BOARDREG_MEM_PRESENT_CODE_4MB 2u
#define AP_BOARDREG_MEM_PRESENT_CODE_NONE 3u
/* "(No Board)  FF" -- the table's first row, and this register's reset value:
 * every pair set is every slot empty. */
#define AP_BOARDREG_MEM_PRESENT_EMPTY 0xFFu

/* ## The DS5500's `010200` is a cache *status* register, and a different part
 *
 * `019411-A00` §4.2.1.14 replaces the handbook's §4.2.1.14 outright: "This
 * 8-bit, **read-only** register exists in the DS5500. This register holds
 * miscellaneous status information." What this core models at `010200` is the
 * Series 4000's cache **control** register -- one writable bit, a measured
 * fixed pattern -- so on a DS5500 the same address is a different register with
 * different bits and no writable ones at all.
 *
 * Figures 4-9 and 4-10 give it twice over, and between them name two bits:
 *
 *     7:4  not used
 *     3    HSI Present   "This bit is cleared (0) to indicate that a graphics
 *                         device is in the HSI connector."
 *     2:1  not used
 *     0    MEM Time      "This bit indicates an access to non-existant memory."
 *                        Figure 4-10 labels the same bit *Memory Timeout*.
 *
 * ## The polarity of bit 0, which the section does not state and the figure does
 *
 * `HSI Present` carries "is cleared (0) to indicate" and `MEM Time` carries only
 * "indicates" -- in the same figure, in the same typeface, written by the same
 * author who marked the active-low one explicitly. So the contrast is evidence
 * rather than an omission: bit 0 reads **1** when a timeout has been seen.
 * Recorded because it is the one place here where a reading rests on how the
 * document is written rather than on what it says.
 *
 * ## Both bits are derived, not stored
 *
 * Neither is state a program can put there. `MEM Time` is the same condition the
 * CPU status register already latches in `AP_BOARDREG_STATUS_BUS_ERROR` -- an
 * access that nothing answered -- so it is reported from there rather than
 * kept twice and cleared in one place; `016408` "Clear Bus Error Status" then
 * clears both by clearing the one. Same reasoning as
 * `AP_BOARDREG_CACHE_INTERRUPT_PENDING`, which is the master controller's line
 * rather than a copy of it.
 *
 * `HSI Present` follows the model's display: a machine with one has a graphics
 * device in the connector. A DSP5500 is the headless variant of exactly this
 * board and has none, which is what makes the bit worth deriving rather than
 * fixing.
 *
 * ## What the unused bits read
 *
 * Not stated, and not measurable -- the oracle has no working DS5500 and this
 * core cannot yet run one. All ones, which is what `FINDINGS.md` C10 measured
 * nothing-driving-this-machine's-bus to look like and what the selective clear
 * range already answers for the same reason. `PROVISIONAL`; a DS5500 that runs
 * would settle it in one read. */
#define AP_BOARDREG_CACHE_STATUS_HSI_PRESENT 0x08u
#define AP_BOARDREG_CACHE_STATUS_MEM_TIME 0x01u
/* Bits 7:4 and 2:1: named "not used" and read as undriven. PROVISIONAL. */
#define AP_BOARDREG_CACHE_STATUS_UNUSED 0xF6u

/* ## The selective clear locations, which are the one part of this file with a
 * ## page behind it
 *
 * Table 2-8 gives `016400`-`0164FF` a row and a name and, as with every other
 * row, no bits. `019411-A00`'s replacement for §4.2.1 does better: it lists the
 * locations *by function*, one address each.
 *
 *     Clear All Locations      00016400
 *     Clear FPU Trap           00016404
 *     Clear Parity Error Flag  00016406
 *     Clear Bus Error Status   00016408
 *     Clear Graphics Trap      0001640A
 *
 * So this range is **not aliased** the way the registers above it are: the low
 * bits select which condition is cleared, and treating the range as one
 * register would make every clear a clear-all. That is why the decode for this
 * id keeps the address instead of dropping it.
 *
 * The boot PROM writes only `016400`, twice, and both times immediately after
 * `clr.w $10000` -- `00168C` and `002632`. Clearing the status register and
 * then clearing everything is the reset path, and neither site nor any other in
 * any image in hand ever *reads* the range.
 *
 * The oracle has this list too, and it differs: it implements `01640E` as
 * "Clear (Flush) Cache", which the addendum does not list, and it does not
 * implement `01640A` at all. The addendum is the authority for both. Graphics
 * trap is decoded and named here and clears nothing, because *which* status bit
 * it is has no source -- and a location that silently cleared the wrong bit
 * would be worse than one that honestly clears none. */
#define AP_BOARDREG_CLEAR_ALL_OFFSET 0x00u
#define AP_BOARDREG_CLEAR_FPU_TRAP_OFFSET 0x04u
#define AP_BOARDREG_CLEAR_PARITY_OFFSET 0x06u
#define AP_BOARDREG_CLEAR_BUS_ERROR_OFFSET 0x08u
#define AP_BOARDREG_CLEAR_GRAPHICS_TRAP_OFFSET 0x0Au

/* ## Bit 0 is the Normal/Service switch, and it is an *input*
 *
 * The status register's power-on value was measured as `8100` and recorded as a
 * constant with the note that "what the bits *mean* is still unknown, and
 * nothing may be built that depends on a meaning". One bit is now known, and it
 * turns out not to be a power-on value at all.
 *
 * `APOLLO_CSR_SR_SERVICE` is `0001`, and the oracle drives it from a machine
 * *configuration* named "Normal/Service" whose two settings are inverted from
 * the obvious reading: `0x00` is **Service** and `0x0001` is **Normal**. So the
 * bit reads **1 for normal operation** and 0 for service, and the constant name
 * says the opposite of what the level means.
 *
 * The oracle's default is **Service**, which is why `8100` was measured: the
 * measurement was of MAME in the configuration it ships in, not of a
 * workstation. `mdsession.lua` had already noticed and says so -- "its default
 * is Service, so leaving it alone is a choice too" -- and sets Normal for an
 * install.
 *
 * That matters because the boot PROM reads it and takes a **completely
 * different path**: in service mode it runs its diagnostics and waits for a
 * console, and in normal mode it goes somewhere else entirely.
 *
 * **The clause that used to end that sentence -- "which is where every boot in
 * this project has ended" -- was stale and is withdrawn.** It described the
 * state before the write-keeps fix below. `CPU_STATUS_RESET` is
 * `ALWAYS_SET | NORMAL_MODE`, `AP_BOARDREG_STATUS_WRITE_KEEPS` preserves bit 0
 * across a status write, and `ap_boardreg_set_normal_mode` is called by
 * nothing, so this core boots in **normal** mode and has since that fix. The
 * diagnostics the console shows are reached for a different reason -- the
 * configuration table in the calendar's battery RAM is empty, and the PROM says
 * so in as many words -- and leaving the old clause in pointed a reader at the
 * wrong cause. So this is a switch a caller sets, with
 * a default of *normal* -- a workstation that boots -- rather than a constant
 * reproducing the oracle's shipping configuration.
 *
 * `FINDINGS.md` C114. */
#define AP_BOARDREG_STATUS_NORMAL_MODE 0x0001u

/* How many posted diagnostic codes to keep. See `ap_boardreg_post_code`. */
#define AP_BOARDREG_POSTED_CODES 32u

/* Bit 15 of the status register reads 1 whatever is written, at every sampled
 * point in the boot. Named for what was observed, because what it *is* was not
 * measured and no manual here says. */
#define AP_BOARDREG_STATUS_ALWAYS_SET 0x8000u

/* ## What a write to the status register keeps, and why it is not "nothing"
 *
 * `008778-03` §3.2 says it plainly, about the parity error interrupt: "The
 * interrupt handler checks the status register to detect which one of these
 * conditions exists. **Writing to the status register clears the interrupt
 * status.**" So the register is a set of *conditions* and a write is how they
 * are acknowledged.
 *
 * Three bits are not conditions and survive:
 *
 * - **Bit 0**, the Normal/Service switch, because it is an *input*. This file
 *   argued that above and then cleared it anyway on every write, which is a
 *   contradiction the firmware walks straight into: `clr.w $10000` at `00168C`,
 *   `002632` and `007440` would each have dropped a normal machine into
 *   service mode.
 * - **Bit 2**, the floating-point trap, because `019411-A00` gives it a
 *   **dedicated clear location** at `016404`. A condition that a status write
 *   already cleared would not need one.
 * - **Bit 15**, which reads set whatever is written or held.
 *
 * The probe that produced `AP_BOARDREG_STATUS_ALWAYS_SET` is consistent with
 * all of this and could not have shown it: it ran against the oracle in
 * *service* mode, where bit 0 is 0, with no FP trap pending, so "reads `8000`
 * after every write" was true and "a write keeps three bits" was invisible
 * underneath it. The test that swept all sixteen bits asserted `8000` too, so
 * it encoded the gap rather than catching it. */
/* Bit 2, the floating-point trap. Named from its clear location at `016404`
 * (`019411-A00`) and corroborated by the oracle's `APOLLO_CSR_SR_FP_TRAP`.
 * Nothing here raises it yet; it is here because a *write* has to know not to
 * clear it. */
#define AP_BOARDREG_STATUS_FP_TRAP 0x0004u

/* Bits 4-7, one per byte lane of the memory array. `008778-03` §3.3: "The
 * parity circuitry for the memory array uses **four** F280 parity
 * checker/generators to generate the parity bits on Write operations and to
 * check the parity bits on Read operations" -- four checkers over 32 data bits
 * is one per byte, which is what a four-bit field in a status register is for.
 * `019411-A00` calls `016406` "Clear Parity Error Flag". */
#define AP_BOARDREG_STATUS_PARITY_MASK 0x00F0u

/* Bit 8, the CPU bus timeout: `019411-A00` names `016408` "Clear Bus Error
 * Status", and this is the bit the oracle clears there. It is also the bit set
 * in the measured power-on value `8100`, which is a machine that has already
 * probed something absent -- not a reset level. */
#define AP_BOARDREG_STATUS_BUS_ERROR 0x0100u

/* What "Clear All Locations" clears: every condition this model holds.
 *
 * The oracle uses `3FFE` -- bits 1 to 13. That is a wider mask than the bits
 * either manual accounts for, and the two differ only in bits nothing here
 * models, so there is nothing to choose between them that any program could
 * see. Written as the union of the named conditions because that is the form
 * that stays correct when the next one is named. */
#define AP_BOARDREG_STATUS_CONDITIONS                                          \
  (AP_BOARDREG_STATUS_FP_TRAP | AP_BOARDREG_STATUS_PARITY_MASK |               \
   AP_BOARDREG_STATUS_BUS_ERROR)

/* The two bits a write to the status register keeps: the switch input, which is
 * an input and not storage, and bit 15, which reads 1 whatever is written.
 *
 * **The FP trap used to be a third, and Domain/OS says it is not.** The reading
 * was that a bit with its own `Clear FPU Trap` location at `016404` must need
 * that location, so a blanket write left it standing. `008778-03` §3.2 says the
 * opposite in general terms -- "Writing to the status register clears the
 * interrupt status" -- and the case could not be told apart by the probe that
 * produced this mask, which ran in *service* mode with no trap pending, where a
 * kept FP trap and a cleared one both read `8000`. The comment on `store` said
 * as much.
 *
 * What settles it is the guest. `FIM_$BUS_ERR` reads this register at `+10`
 * (`MOVE.W ($3FFFB400).L,D1`) and writes it at `+20`, 340 times in a boot --
 * read-then-acknowledge, which means nothing unless the write clears what was
 * read. And Domain/OS **never writes `016404` at all**: the only write to it in
 * a whole boot is the PROM's, at `01004700`, tidying up after its own
 * `CPU (fp trap)` self test. So if a status write did not clear this bit, the
 * first lazy-floating-point trap would latch it for ever, `FIM_$BUS_ERR` would
 * read it set on every subsequent bus error, and the kernel could never page
 * again -- which is exactly what this core did, and Domain/OS plainly did not
 * do on the hardware it shipped on.
 *
 * The selective location keeps its purpose: clearing *only* the FP trap and
 * leaving the other conditions standing, which a blanket write cannot do. */
#define AP_BOARDREG_STATUS_WRITE_KEEPS                                         \
  (AP_BOARDREG_STATUS_ALWAYS_SET | AP_BOARDREG_STATUS_NORMAL_MODE)

/* ## The control register's low byte, which the firmware writes separately
 *
 * The LEDs are the register's **upper** byte -- `008778-03` §3.7, "nine LED
 * indicators for diagnostics that can be set or reset by writing to the upper
 * byte of the control register" -- and the firmware byte-writes them at
 * `010100`. It also byte-writes `010101`, the **lower** byte, and those writes
 * are the parity diagnostic's.
 *
 * This model treated the two addresses alike, so the parity writes were
 * arriving as LED codes: a boot's posted-code list ended `... 8F 08 00 01`,
 * where `08 00 01` is the parity test at `00744E`, `00745C` and `00746C` and
 * not a diagnostic code at all. Byte lane is address bit 0, big-endian, which
 * is what a 16-bit register on this bus is.
 *
 * Bit 3, force bad parity, and bits 4-7, the byte lanes it applies to.
 * `008778-03` §3.3 is where the four comes from -- "four F280 parity
 * checker/generators" over 32 data bits, one per byte -- and the same section
 * says the circuitry "can be forced bad by inputting to the F280 and writing
 * into the parity RAM in diagnostic mode".
 *
 * **The lane bits are active low on this family**, and the firmware proves it
 * without the oracle's help. The DS3500 and DS4500 PROMs write `08` -- bit 3
 * set, all four lane bits *clear* -- and then require all four status bits to
 * come back set. The two DN3000 PROMs run the same test and write `F8`: bit 3
 * set and all four lane bits **set**. Two families, complementary values, one
 * behaviour. `3000_BOOT_8475_7` at `006848`, `3500_BOOT_12191_7` at `00744E`,
 * `4500_BOOT_13167_02` at `00746E`, `5500_BOOT_A1631-80046` at `007BFE`.
 *
 * What no image settles is **which lane bit is which byte**: every one of them
 * drives all four together, so the four-bit field is only ever `0` or `F`. The
 * assignment below is therefore `PROVISIONAL` -- see `ap_parity.h`. */
/* Bit 2, and it disconnects the coprocessor rather than arming anything.
 *
 * The loaded diagnostic's `CPU (fp trap)` test is the whole specification: at
 * `01004698` it saves the F-line vector and installs its own, writes `0004`
 * here, executes `FMOVE.L D0,FP0`, and requires the **status** register to read
 * `0004` afterwards. So setting this bit makes a coprocessor instruction take
 * F-line, and taking it is what sets the status register's FP trap bit.
 *
 * The oracle sets that status bit *here*, at the control-register write, and
 * says in its own comment that it should not: "hack: set APOLLO_CSR_SR_FP_TRAP
 * in cpu status register for /sau7/self_test -- APOLLO_CSR_SR_FP_TRAP in status
 * register should be set by next fmove instruction". It also guards the hack on
 * the MMU being off, which is a condition with no hardware meaning and which
 * would fail here, since this diagnostic runs with translation enabled. This
 * core does what the comment describes instead. */
#define AP_BOARDREG_CONTROL_FPU_TRAP 0x0004u

/* ## Bit 0 is `nme`, and the gate it names was already here
 *
 * `002398-04` p. 12-8 gives the control register's low nibble as `dg` enable
 * diag mode, `fp` enable fp owner trap, `rsa` reset on-board devices and `nme`
 * **non-maskable interrupt enable**, and then states the gate outright:
 * "**Non-maskable interrupts must be enabled to receive parity errors.**"
 *
 * That is `ap_board_parity_interrupt`, which has required this bit since it was
 * written -- derived from the boot PROM's self-test 7, which sets it before
 * forcing a parity error and clears it afterwards. So the handbook is a second,
 * independent statement of a rule this core already implements, and the item
 * that carried it as an open question was wrong to say it was not modelled.
 * Renamed from `INTERRUPT_ENABLE` to the name the document uses, because "which
 * interrupt" is the whole content of the sentence. */
#define AP_BOARDREG_CONTROL_NMI_ENABLE 0x0001u

/* ## Bit 1 is `rsa`, "reset on-board devices" -- with one named exclusion
 *
 * p. 12-8: "**Neither RSA nor the reset instruction reset the SIO lines.**"
 * That sentence does two things. It excludes the SIO from what this bit
 * touches, and it says RSA and the 68030's `RESET` instruction have the *same*
 * effect -- which is what makes the exclusion worth stating at all.
 *
 * ## What it resets, and what it does not
 *
 * The page says "on-board devices" and enumerates nothing, so the list is drawn
 * from what a reset line on this board can reach and each exclusion is stated:
 *
 *   - the two 8259 interrupt controllers, the two 8237 DMA controllers, the DMA
 *     page registers and the 6840 timer -- **reset**;
 *   - the **SIO**, excluded by the sentence above, and with it the keyboard and
 *     the beeper, which are behind it;
 *   - the **calendar**, excluded because it is battery-backed. Its RAM holds
 *     the node ID and the configuration table, and a machine that lost those on
 *     a register write would be a machine that could not boot -- so a reset
 *     that cleared them would be inventing a failure this hardware does not
 *     have. The MC146818's own RESET pin clears two interrupt-enable bits and
 *     leaves the clock and the RAM standing, which is the same conclusion from
 *     the part's side.
 *
 * ## Why implementing it cannot move the reference boot
 *
 * **Every boot PROM on this shelf contains the RSA pulse and branches over
 * it.** In `3500_BOOT_12191_7` the sequence is
 *
 *     0073FC  BRA.W   $007418        <-- jumps past the whole block
 *     007400  MOVE.B  #$02,$010101   ; rsa asserted
 *     007408  MOVE.W  #$7FFF,D0
 *     00740C  DBRA    D0,$00740C     ; the pulse width
 *     007410  MOVE.B  #$00,$010101   ; rsa released
 *     007418  BSR.S   ...            <-- the branch's target
 *
 * and both DN3000 revisions have the identical shape at `0067F6` and `0067A2`.
 * Nothing in any image references `007400` absolutely and no branch of any kind
 * lands inside the block, so it is disabled code in every shipped PROM here --
 * someone put a `BRA` over a reset that presumably misbehaved. That is why this
 * could be implemented without a boot to measure it against: the firmware never
 * executes the write.
 *
 * **The reset *instruction* is a different matter and is not wired.** The three
 * `RESET` opcodes in each PROM may well execute, so giving them this effect is
 * a behaviour change on a path the boot may take, and it belongs behind a
 * measurement rather than in front of one. The CPU counts them already --
 * `external_resets` in `ap_m68030_step.h`, "counted rather than acted on: this
 * module has no external devices" -- and the wiring point is `ap_machine_step`,
 * which holds both halves. Named in `docs/COMPLETION_PLAN.md`. */
#define AP_BOARDREG_CONTROL_RESET_DEVICES 0x0002u

#define AP_BOARDREG_CONTROL_FORCE_BAD_PARITY 0x0008u
#define AP_BOARDREG_CONTROL_PARITY_LANE_MASK 0x00F0u

/* The cache control register is eight bits, not sixteen -- measured, and the
 * single most valuable thing the probe found, because a transcription working
 * from Table 2-8's uniform-looking rows would have made it a word like its
 * neighbours. The byte is aliased across the range, so a 16-bit read returns it
 * twice.
 *
 * Only bit 7 is writable. The rest read `6F` in every sample taken. */
#define AP_BOARDREG_CACHE_WRITABLE 0x80u
#define AP_BOARDREG_CACHE_FIXED 0x6Fu

/* ## Bit 4 is **interrupt pending**, and the probe could not have seen it
 *
 * The "fixed pattern" above is what the register reads with **no interrupt
 * standing**, which is every sample a register probe takes: it drives bits and
 * reads them back on a quiet machine. Bit 4 is not storage and not fixed -- it
 * follows the master 8259's `INT` output, set while the controller is asking
 * and cleared when the processor acknowledges.
 *
 * Two sources agree and the firmware is the third. The oracle sets it from the
 * master's interrupt line on everything that is not a DN3000 -- "set bit
 * Interrupt Pending in Cache Status Register", `0x10` -- and clears it in the
 * acknowledge path; a DN3000 uses the *status* register's bit 3 instead, which
 * is why `008778-03`'s status-register bit has no meaning on this machine. And
 * the loaded `SELF_TEST` diagnostic reads `010200` at `01002848` immediately
 * after unmasking the cascade on controller 1, and requires this bit set.
 *
 * So it is derived here rather than latched: a stored copy would need clearing
 * on acknowledge, and the line already does that. Same reasoning as the parity
 * interrupt in `board/ap_parity.h`. */
#define AP_BOARDREG_CACHE_INTERRUPT_PENDING 0x10u

typedef struct {
  uint16_t cpu_status;
  /* The diagnostic codes posted to the control register, oldest first and
   * distinct-in-order. See below. */
  uint8_t posted[AP_BOARDREG_POSTED_CODES];
  unsigned posted_count;
  unsigned posted_total;
  uint16_t cpu_control;
  uint8_t cache_control;
  uint16_t latch_page_on_parity;
  /* A model difference, not a measurement of this board: see
   * `ap_boardreg_set_active_low_lanes`. */
  bool active_low_parity_lanes;
  /* The master 8259's `INT`, refreshed by the board -- see the cache register's
   * bit 4 above. Not storage: nothing a program writes can change it. */
  bool interrupt_pending;
  /* Table 2-8's remaining two, byte-wide storage and no interpretation. */
  uint8_t master_request;
  uint8_t task_alias;
  /* Not storage in the sense the others are: read-only, and what it holds is
   * how much memory is fitted rather than anything software put there. */
  uint8_t memory_present;
  /* Whether `010200` is the DS5500's read-only *status* register rather than
   * the Series 4000's cache control register. A model difference, set from the
   * table exactly as `active_low_parity_lanes` is -- never decided here. */
  bool ds5500_cache_status;
  /* "a graphics device is in the HSI connector". Set by the board from the
   * model's display; the bit it drives is active low. */
  bool hsi_graphics_present;
} ap_boardreg_t;

/* Reset to the measured power-on values.
 *
 * These are measurements and not choices: the probe was run at 0.001, 0.5 and
 * 2.0 emulated seconds and read the same value each time, so they are stable
 * rather than a snapshot of something mid-boot. What is *not* established is
 * that they are what the hardware holds at the instant reset releases -- the
 * earliest sample still follows some PROM execution. Stable across three orders
 * of magnitude of boot time is the strongest claim available here. */
void ap_boardreg_init(ap_boardreg_t *regs);

/* Which byte lanes a write under this register file gives bad parity, as a
 * four-bit field in bits 4-7. Zero when the register is not forcing. */
[[nodiscard]] uint16_t ap_boardreg_forced_lanes(const ap_boardreg_t *regs);

/* Whether this board's lane bits are active low. Defaults to true, the DN3500's
 * -- the reference superset, as everywhere else here -- and a DS3000 board sets
 * it false from `ap_model_t::has_active_low_parity_lanes`. */
void ap_boardreg_set_active_low_lanes(ap_boardreg_t *regs, bool active_low);

/* Make `010200` the DS5500's cache **status** register: `019411-A00` §4.2.1.14,
 * read-only, and a different set of bits from the register this core measured
 * at that address on a DN3500. Set from the model table by the board. */
void ap_boardreg_set_ds5500_cache_status(ap_boardreg_t *regs, bool ds5500);

/* Whether a graphics device occupies the HSI connector. Drives `HSI Present`,
 * which is cleared when one is. */
void ap_boardreg_set_hsi_graphics(ap_boardreg_t *regs, bool present);

/* The master interrupt controller's request line, which the cache register
 * reports in bit 4. Called by the board whenever it samples its devices. */
void ap_boardreg_set_interrupt_pending(ap_boardreg_t *regs, bool pending);

/* Whether the control register is holding the coprocessor off the bus. */
[[nodiscard]] bool ap_boardreg_fpu_trapped(const ap_boardreg_t *regs);

/* Which register an address decodes to, if any. False for the declined pair as
 * well as for unmapped addresses; use `ap_boardreg_is_declined` to tell them
 * apart. */
[[nodiscard]] bool ap_boardreg_decode(uint32_t address, ap_boardreg_id_t *out);

/* True for task alias and master request: registers Table 2-8 names, that this
 * core deliberately does not model. See the header. */
[[nodiscard]] bool ap_boardreg_is_declined(uint32_t address);

/* The master request register's byte, exactly as written.
 *
 * `008778-03` §2.4.7 says setting a bit here asserts an external master's DMA
 * request; it does not say which bit, and the five boot PROMs drive bits 1, 3
 * and 6 without ever reading the register back. So this reports the byte and
 * makes no claim about what any bit does. */
[[nodiscard]] uint8_t ap_boardreg_master_request(const ap_boardreg_t *regs);

/* The two-bit code §4.2.1.18's table gives a board of this size, and
 * `AP_BOARDREG_MEM_PRESENT_CODE_NONE` for an empty slot -- `megabytes` zero.
 *
 * False, with `*out` untouched, for any other size. All four codes are spoken
 * for, so there is nothing left to encode a 32 MB board as; a machine fitted
 * with one is a question this register cannot answer and the caller must not be
 * handed a value that looks like it did. */
[[nodiscard]] bool ap_boardreg_memory_present_code(unsigned megabytes,
                                                   unsigned *out);

/* Assemble the register from four slot sizes in megabytes, slot 0 first, zero
 * for an empty slot. False and no change if any slot carries a size the table
 * does not list. */
[[nodiscard]] bool ap_boardreg_set_memory_boards(ap_boardreg_t *regs,
                                                 const unsigned *slot_megabytes,
                                                 unsigned slots);

/* What `011400` reads on a machine that has one. */
[[nodiscard]] uint8_t ap_boardreg_memory_present(const ap_boardreg_t *regs);

/* Access. A 16-bit read of the cache control register returns its byte in both
 * halves, which is what the hardware was measured to do rather than a
 * convenience. Reads of an address that decodes to nothing return 0 and writes
 * are dropped; a caller wanting bus-error semantics for unmapped space should
 * ask `ap_boardreg_decode` first, which is why it is public. */
[[nodiscard]] uint16_t ap_boardreg_read16(const ap_boardreg_t *regs,
                                          uint32_t address);
[[nodiscard]] uint8_t ap_boardreg_read8(const ap_boardreg_t *regs,
                                        uint32_t address);
void ap_boardreg_write16(ap_boardreg_t *regs, uint32_t address, uint16_t value);
void ap_boardreg_write8(ap_boardreg_t *regs, uint32_t address, uint8_t value);

/* Raise bits in the status register.
 *
 * Present because the write-clear behaviour cannot otherwise be exercised: a
 * register that reads `8000` after every write looks the same whether writes
 * clear it or writes are ignored, and only setting a bit first distinguishes
 * them. The measurement did distinguish them -- the initial `8100` could not be
 * restored -- so the mechanism is real.
 *
 * `mask` is a raw bit mask on purpose. There is no enumeration of conditions
 * here because no source here says which bit is which condition, and a named
 * constant would be a guess wearing a name. */
void ap_boardreg_latch_status(ap_boardreg_t *regs, uint16_t mask);

/* Set the Normal/Service switch. `true` is normal, which is the default and is
 * what a workstation runs in; `false` is service, which is what the oracle
 * ships in and what every boot in this project ran under before the switch was
 * identified. */
void ap_boardreg_set_normal_mode(ap_boardreg_t *regs, bool normal);

/* ## The diagnostic code display
 *
 * The control register at `010100` is also the machine's **diagnostic LEDs**.
 * `FINDINGS.md` C109 found the post routine at `00251A`, which takes its
 * argument inline and ends `MOVE.B D0,(A1)` with `A1` pointing here -- and the
 * byte is written **complemented**, so a posted `03` arrives as `FC`.
 *
 * A machine that fails a self-test posts a code, waits, posts another and waits
 * again, for ever. This core was counting those writes and discarding the
 * values, which is throwing away the one thing the firmware says about what
 * went wrong. The codes are kept now, oldest first, and a boot reports them.
 *
 * **The firmware has a routine whose whole job is that alternation**, and it is
 * named. `002398-04` p. 10-23 and p. 11-9 document the boot PROM's service table
 * at offset `100`; every PROM in `roms/firmware/` carries it, and the DN3500's
 * eleventh entry, **`led_update`, is `0000254E`**. Disassembled:
 *
 *     254E  2F0E             MOVE.L  A6,-(SP)
 *     2550  6100 E0AC        BSR.W   $05FE
 *     2554  6104             BSR.S   $255A
 *     2556  2C5F             MOVE.L  (SP)+,A6
 *     2558  4E75             RTS
 *     255A  48E7 E0C0        MOVEM.L D0-D2/A0-A1,-(SP)
 *     255E  0C2E 00FF 01C9   CMPI.B  #$FF,($1C9,A6)
 *     2564  67E2             BEQ.S   $2548
 *     2566  102E 01D5        MOVE.B  ($1D5,A6),D0
 *     256A  E148             LSL.W   #8,D0
 *     256C  102E 01D4        MOVE.B  ($1D4,A6),D0
 *     2570  3D40 01D4        MOVE.W  D0,($1D4,A6)
 *
 * The three instructions from `2566` build a word whose high byte is the byte at
 * `A6+$1D5` and whose low byte is the one at `A6+$1D4`, then store it back over
 * both -- which on a big-endian bus **swaps the pair**. So the firmware keeps two
 * display bytes and exchanges them each time `led_update` is called, guarded by
 * a byte at `A6+$1C9` that suppresses the whole thing when it reads `FF`.
 *
 * That is the alternation described above, from the other side: it was inferred
 * from the *observed write sequence*, and here is the code that produces it. Two
 * codes, swapped, for ever -- which is why `AP_BOARDREG_POSTED_CODES` keeps
 * distinct values in order rather than a ring of every write, and why a stuck
 * machine shows exactly two.
 *
 * **The byte is recorded exactly as written**, and that is deliberate. The post
 * routine complements what it displays, but the firmware also writes this
 * register *directly* in places -- `005EC8` and `005ED8` do, in the error
 * loop -- and those are not complemented. Undoing the complement here would be
 * right for one caller and wrong for the other, so the raw byte is kept and the
 * reader is told which is which rather than the model guessing.
 *
 * **Both halves of that are now read out of the ROM rather than inferred.** The
 * post routine at `00251A` ends:
 *
 *     2536  4600             NOT.B   D0
 *     2538  1D40 01D5        MOVE.B  D0,($1D5,A6)
 *     253C  1D7C 00FF 01D4   MOVE.B  #$FF,($1D4,A6)
 *     2542  226E 015A        MOVEA.L ($15A,A6),A1
 *     2546  1280             MOVE.B  D0,(A1)
 *
 * -- so the complement is a literal `NOT.B`, the register's address is held at
 * `A6+$15A` rather than being immediate, and **the code is paired with a
 * constant `FF`** at `A6+$1D4`. That last is why a boot's posted sequence
 * alternates every code with `FF`: it is the pair `led_update` swaps, and it is
 * one code displayed, not two.
 *
 * The routine also has **two entry points**. `251A` fetches its code from the
 * word *after* the call and steps the return address over it; `252A` skips that
 * and takes the code already in `D0`, for callers that compute one. A scan of
 * this ROM finds fourteen sites on the first and four on the second, so a
 * computed code cannot be recovered from its call site at all.
 *
 * **And the register has a third kind of writer, which is why the complement
 * must never be applied blindly.** Searching this ROM for the absolute address
 * `00010100` finds 33 references, among them a run of
 * `13FC 00xx 0001 0100` -- `MOVE.B #imm,($00010100).L`. At `00653E`, `006560`,
 * `0065C8`, `00660E` and `006648` the immediates are `EF`, `DF`, `FE`, `EE` and
 * `DE`, which are the *first bytes a boot posts* and are written
 * **uncomplemented**. Under p. 12-8's "1 => led off" they are lamp patterns --
 * all dark, then one, two and three lamps lit -- a power-on progress display
 * rather than test codes.
 *
 * So a posted byte can come from the post routine (complemented), the error
 * loop at `005EC8` (raw, one code shown whole and as its low nibble), or these
 * immediates (raw lamp patterns). `ap_boardreg_post_code_name` names only
 * complements that land in `03`-`0C`, which is why it stays silent on all of the
 * third kind -- `EF` complements to `10` and is refused. The LED-decode item in
 * `docs/COMPLETION_PLAN.md` carries the rest.
 *
 * Only the distinct ones in order: an error loop posts the same pair for ever
 * and a ring of every write would hold nothing but the last two. */

/* Record a byte written to the control register as a posted code, exactly as
 * written.
 *
 * **The byte is active low, and it is stored uninverted on purpose.**
 * `002398-04` p. 12-8 gives the control register's upper byte as `ld7`-`ld0`
 * with "**1 => led off**", so `FF` is every diagnostic lamp *dark* and `00` is
 * every lamp *lit*. Storing the register byte rather than a lamp state is the
 * right choice -- this is a record of what the firmware wrote, and inverting it
 * would put an interpretation between the write and the report -- but the
 * polarity has to be written down or the report is unreadable.
 *
 * The reference boot's own sequence is the evidence that the polarity is that
 * way round: it opens `FF EF DF FE EE DE CF BF AF 9F 8F ...` and reaches `00`
 * exactly once. Under "1 => off" that is a machine starting with every lamp
 * dark and lighting them as it goes, which is what a power-on self-test display
 * does. Under the opposite reading it would start with all nine lit and stay
 * mostly lit, which is not a progress indicator.
 *
 * So a reader of `--boot-report`'s `posted codes` line should read a **clear**
 * bit as a lit lamp. `008778-03` §3.7 gives the physical arrangement: nine
 * LEDs, of which the green `PWR` is not used for diagnostics, leaving eight --
 * four on the CPU board and four on the front panel labelled A B C D. */
void ap_boardreg_post_code(ap_boardreg_t *regs, uint8_t written);

/* ## Naming a posted code, for `03`-`0C` only
 *
 * `002398-04` p. 4-23 tabulates the DN3000's power-on tests against their LED
 * codes, and **the DN3500 uses the same numbering**. That is established, not
 * assumed: the post routine's call sites in `3500_BOOT_12191_7` supply `03`
 * through `0B` inline and `0C` as an immediate at `000930`, which is ten
 * consecutive codes against the table's ten consecutive entries. The plan item
 * carries the sites.
 *
 * **Only that run is named.** `0D` (Calendar and configuration) has no site
 * whose code could be read -- two callers take theirs from a variable -- and the
 * `82`-`85` band the same ROM posts is outside the table entirely. Naming those
 * would be inventing a decode for the third of the range that is *not*
 * evidenced, in a report whose whole value is that a reader can trust it.
 *
 * **The argument is the byte as written, and it is complemented here.** The post
 * routine ends `NOT.B D0` before storing, so a posted `03` reaches the register
 * as `FC`. Beware that this cannot be applied blindly to every byte in the
 * posted list: the error loop at `005EC8` writes its code *uncomplemented*, and
 * a raw `03` from that path would decode as `FC`'s name if it were fed here. The
 * caller decides which bytes are post-routine writes; this function only names
 * one once that is decided.
 *
 * Returns nullptr for anything outside the evidenced run. */
[[nodiscard]] const char *ap_boardreg_post_code_name(uint8_t written);

#endif /* APOLLO_BOARD_AP_BOARDREG_H */
