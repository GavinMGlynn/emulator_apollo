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
 * guess at", which are very different facts about the machine.
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
