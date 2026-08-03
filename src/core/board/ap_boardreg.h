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
 * ## The two registers this module refuses
 *
 * Task alias (`010300`) and master request (`011600`) are **declined**, not
 * missing. They read all-ones with no writable bit -- and so do two addresses
 * chosen from gaps in Table 2-8, which is how C10 established that all-ones is
 * simply what unmapped looks like on this machine. They are absent from the
 * *oracle*; Table 2-8 lists them, so the hardware has them.
 *
 * Modelling them as all-ones would copy an oracle gap into this core wearing
 * the clothes of a measurement. `ap_boardreg_is_declined` exists so a caller
 * can tell "no register here" from "a register we know exists and refuse to
 * guess at", which are very different facts about the machine -- and
 * `board/ap_board.h` now counts both, so a run says which one it touched.
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
#define AP_BOARDREG_LATCH_PAGE_ADDR 0x011300u
#define AP_BOARDREG_MASTER_REQUEST_ADDR 0x011600u

/* Bit 15 of the status register reads 1 whatever is written, at every sampled
 * point in the boot. Named for what was observed, because what it *is* was not
 * measured and no manual here says. */
#define AP_BOARDREG_STATUS_ALWAYS_SET 0x8000u

/* The cache control register is eight bits, not sixteen -- measured, and the
 * single most valuable thing the probe found, because a transcription working
 * from Table 2-8's uniform-looking rows would have made it a word like its
 * neighbours. The byte is aliased across the range, so a 16-bit read returns it
 * twice.
 *
 * Only bit 7 is writable. The rest read `6F` in every sample taken. */
#define AP_BOARDREG_CACHE_WRITABLE 0x80u
#define AP_BOARDREG_CACHE_FIXED 0x6Fu

typedef struct {
  uint16_t cpu_status;
  uint16_t cpu_control;
  uint8_t cache_control;
  uint16_t latch_page_on_parity;
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

/* Which register an address decodes to, if any. False for the declined pair as
 * well as for unmapped addresses; use `ap_boardreg_is_declined` to tell them
 * apart. */
[[nodiscard]] bool ap_boardreg_decode(uint32_t address, ap_boardreg_id_t *out);

/* True for task alias and master request: registers Table 2-8 names, that this
 * core deliberately does not model. See the header. */
[[nodiscard]] bool ap_boardreg_is_declined(uint32_t address);

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

#endif /* APOLLO_BOARD_AP_BOARDREG_H */
