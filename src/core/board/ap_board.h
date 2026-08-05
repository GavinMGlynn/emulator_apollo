/* The DN3500 core board's address map.
 *
 * `008778-03` Table 2-8, the 64 MB physical address space allocation. Every
 * device this core has is placed by that table and by the measurements that
 * confirmed each placement; nothing here is arranged for convenience.
 *
 * ## Why this exists
 *
 * `machine/ap_machine.h` wires the processor to flat RAM from zero. That was
 * right for probes -- a probe wants its program at a known address and nothing
 * else in the way -- and it is the wrong shape for firmware. Running the
 * Domain/OS boot image on flat RAM produced 5634 bus errors and a final PC
 * outside memory (`FINDINGS.md` C28), because the firmware reaches for a machine
 * whose RAM is at `1000000` and whose devices are at fixed low addresses.
 *
 * This module is that machine's shape. It does not replace `ap_machine`: the
 * probes keep their flat memory, because a probe harness that had to be a whole
 * DN3500 would be a worse probe harness.
 *
 * ## What answers, and what does not
 *
 * A region no device claims is reported as unmapped rather than silently
 * reading zero. That distinction is the one C28 turned on: flat RAM made every
 * device address read as zero, which hid thousands of accesses that should have
 * been visible. An emulator that answers everything cannot tell you what the
 * firmware wanted.
 */

#ifndef APOLLO_BOARD_AP_BOARD_H
#define APOLLO_BOARD_AP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "board/ap_arbiter.h"
#include "model/ap_model.h"
#include "board/ap_atbus.h"
#include "board/ap_atmap.h"
#include "board/ap_boardreg.h"
#include "board/ap_parity.h"
#include "board/ap_calendar.h"
#include "board/ap_disk.h"
#include "board/ap_dma.h"
#include "board/ap_dmapage.h"
#include "board/ap_intr.h"
#include "board/ap_nodeid.h"
#include "board/ap_sio.h"
#include "board/ap_graphics.h"
#include "device/ap_kbd.h"
#include "board/ap_tape.h"
#include "board/ap_timer.h"

/* `008778-03` Table 2-8. */
#define AP_BOARD_PROM_BASE 0x000000u
#define AP_BOARD_PROM_SIZE 0x010000u
#define AP_BOARD_RAM_BASE 0x1000000u
/* The largest main memory a DN3500 takes, and so the end of the address space
 * allocated to it. The oracle's map is built with `DN3500_RAM_END` at
 * `017FFFFF`, `01FFFFFF` or `03FFFFFF` for 8, 16 or 32 Mbyte -- the base is
 * fixed and the top moves with what is fitted, so the *space* ends at the
 * largest of them.
 *
 * Bounded rather than open-ended because the region enum's whole purpose is to
 * answer "what did the firmware reach for". Claiming main memory for every
 * address above the base made a final PC of `FFFF060E` print as "main memory",
 * which is a worse answer than "unmapped": the read path refused it correctly,
 * so the only thing wrong was the name, and a confident wrong name is what a
 * reader acts on. */
#define AP_BOARD_RAM_LIMIT 0x3FFFFFFu

/* The two AT bus windows. `008778-03`, and confirmed against the oracle's
 * `dn3500_map`: `ATBUS_IO_BASE 0x040000`, `ATBUS_IO_END 0x05ffff`,
 * `ATBUS_MEMORY_BASE 0x080000`, `ATBUS_MEMORY_END 0xffffff`.
 *
 * Both windows are **decoded by the board**, not by whatever card is in them.
 * An address in a window with no card behind it reads `FF` -- the bus is
 * pulled up and the cycle terminates -- and does *not* bus error. That is the
 * same distinction the display controller turned on: "no card is fitted" and
 * "nothing decodes this address" are different answers, and only the second is
 * a fault.
 *
 * Which matters here because the boot PROM *jumps into* AT bus memory at
 * `00090000`, almost certainly scanning for an expansion ROM. A machine that
 * faulted on the window would turn a scan that finds nothing into a crash. */
#define AP_BOARD_ATBUS_IO_BASE 0x040000u
#define AP_BOARD_ATBUS_IO_END 0x05FFFFu
#define AP_BOARD_ATBUS_MEMORY_BASE 0x080000u
#define AP_BOARD_ATBUS_MEMORY_END 0xFFFFFFu

/* Which part of the machine an address belongs to. Named rather than
 * boolean-decoded so a caller -- or a trace -- can say *what* the firmware
 * reached for, which is the question C28 could not answer. */
typedef enum {
  AP_BOARD_REGION_UNMAPPED = 0,
  AP_BOARD_REGION_PROM,
  AP_BOARD_REGION_CORE_REGISTER,
  AP_BOARD_REGION_SIO,
  AP_BOARD_REGION_TIMER,
  AP_BOARD_REGION_CALENDAR,
  AP_BOARD_REGION_DMA,
  AP_BOARD_REGION_INTERRUPT,
  AP_BOARD_REGION_NODE_ID,
  AP_BOARD_REGION_TRANSLATION_MAP,
  /* The DS3000's counterpart to the map: `board/ap_dmapage.h`. */
  AP_BOARD_REGION_DMA_PAGE,
  AP_BOARD_REGION_DISK,
  AP_BOARD_REGION_TAPE,
  AP_BOARD_REGION_GRAPHICS,
  AP_BOARD_REGION_ATBUS,
  AP_BOARD_REGION_RAM,
} ap_board_region_t;

/* One past the last region, for sizing a per-region array. Kept beside the
 * enum so a region added without extending the counters is a compile error in
 * the switch statements rather than a silently unrecorded one here. */
#define AP_BOARD_REGIONS (AP_BOARD_REGION_RAM + 1u)

/* One device's placement on one model's board.
 *
 * `canonical` is the address the *device module* expects, which is the DN3500's
 * -- every module in `board/` carries its own placement from Table 2-8 and this
 * core was built around them. A DS3000 board puts the same devices elsewhere, so
 * the map translates: a machine address in `[base, base + size)` becomes
 * `canonical + (address - base)` before the module ever sees it.
 *
 * Doing it that way rather than parameterising every module keeps the placement
 * variance in one table, which is `CLAUDE.md`'s rule, and leaves the modules
 * saying what their own manual says. */
typedef struct {
  uint32_t base;
  uint32_t size;
  ap_board_region_t region;
  uint32_t canonical;
} ap_board_placement_t;

/* One model's physical address space. `008778-03` Table 2-6 for the DS3000 and
 * Table 2-8 for the DS4000, which is the map this core was written against. */
typedef struct {
  const char *name;
  const ap_board_placement_t *placement;
  unsigned placements;
  uint32_t ram_base;
  uint32_t ram_limit;
  uint32_t prom_size;
  /* `008778-03` §1.2: "The Series 4000, unlike the Series 3000, incorporates an
   * address translation map in its architecture", so a DS3000's DMA reaches
   * physical memory directly and the map's window is not decoded at all. */
  bool has_translation_map;

  /* Address bits this board's decode keeps. `008778-03` §1.3: "In the Series
   * 3000, the virtual address appears to 'wrap' at 26 bits, the five high-order
   * (27:31) bits are simply ignored. The Series 4000 makes use of all virtual
   * address bits."
   *
   * The sentence disagrees with itself by one -- ignoring bits 27 through 31
   * leaves twenty-seven, not twenty-six -- and the disagreement has no
   * observable consequence: Table 2-6's space ends at `FFFFFF`, so every
   * address above it is unmapped under either reading and the only addresses
   * the mask changes are ones that wrap onto the same place. The explicit
   * clause is implemented and the ambiguity recorded rather than resolved by
   * preference. */
  uint32_t address_mask;
} ap_board_map_t;

[[nodiscard]] const ap_board_map_t *ap_board_map_for(ap_model_id_t model);

typedef struct ap_board {
  /* Which model's address space this board lays out. */
  const ap_board_map_t *map;

  ap_boardreg_t registers;
  /* The memory array's parity circuit. Inert until a caller fits the parity
   * RAM with `ap_board_attach_parity`; see `board/ap_parity.h`. */
  ap_parity_t parity;
  ap_atmap_t translation_map;
  ap_intr_t interrupts;
  ap_timer_t timer;
  ap_calendar_t calendar;
  ap_dma_t dma;
  ap_dmapage_t dma_page;
  ap_sio_t sio;
  ap_nodeid_t node_id;
  ap_disk_t disk;
  ap_tape_t tape;
  ap_graphics_t graphics;
  ap_kbd_t keyboard;

  /* Which appendix's AT bus cycle times this board keeps. **`PROVISIONAL`**:
   * `008778-03` covers the DS3000 and DS4000, our reference machine is a
   * DS3500, and `019411-A00` -- the addendum that does cover it -- publishes no
   * bus cycle times at all. `board/ap_atbus.h` states the bracket the two
   * published sets give and what closing it needs. Series 3000 is the set here
   * because this is a Series 3000-family board; the disagreement the other
   * choice would produce is one bus clock on a memory read. */
  ap_atbus_series_t at_bus_series;

  /* The boot PROM, caller-owned. NULL until one is loaded, and the region then
   * answers unmapped -- a machine with no PROM is a real configuration and must
   * not look like one with a blank PROM. */
  const uint8_t *prom;
  uint32_t prom_bytes;

  /* Main memory, caller-owned as `ap_machine`'s is: the core allocates
   * nothing. */
  uint8_t *ram;
  uint32_t ram_bytes;

  /* Counted per region, so "the firmware wanted the calendar" is answerable.
   * A single bus-error total says only that something was missing. */
  unsigned unmapped_reads;
  unsigned unmapped_writes;

  /* The first unmapped address in each direction, which a count alone cannot
   * give. "129 unmapped reads" is a number; "129 unmapped reads, the first at
   * `0002xxxx`" is a lead. Recording the first rather than the last because a
   * run that goes wrong tends to keep going wrong, and the earliest one is the
   * one with the fewest causes behind it. */
  uint32_t first_unmapped_read;
  uint32_t first_unmapped_write;

  /* Writes to a read-only memory, which are absorbed rather than refused and so
   * are *not* unmapped. Kept apart because the two mean opposite things: an
   * unmapped write is an address nothing answers, while this is an address
   * something answers and cannot store. Folding them together would both hide a
   * driver writing to a PROM and make a harmless write look like a fault. */
  unsigned rom_writes;
  uint32_t first_rom_write;

  /* The board's one arbitration point. Held here because there is one bus and
   * one priority order for it, and a machine that gave each master its own
   * would have no contention to be emergent. */
  ap_arbiter_t arbiter;

  /* Blit cycles that named one of `CR0`'s two undescribed modes. Counted rather
   * than guessed: nothing names modes 5 and 6, so a run that reaches one is a
   * run whose picture cannot be trusted, and a silent store would hide that
   * behind a plausible image. The first offset is kept for the same reason
   * every other counter here keeps one -- a total cannot say *where*. */
  /* The RAM configuration strapped onto serial 1's input port, and whether the
   * model and size were a pair the oracle's table covers. A run whose machine
   * cannot tell its firmware how much memory it has should say so rather than
   * present a silent zero -- which reads to the PROM as no memory at all. */
  uint8_t ram_config;
  bool ram_config_known;

  unsigned graphics_unknown_mode_cycles;
  uint32_t first_graphics_unknown_mode;

  /* Blit cycles into the image memory, and how many planes they wrote between
   * them. Separate from `region_writes[GRAPHICS]`, which counts the *register*
   * block and the memory together and so cannot tell "the firmware never wrote
   * a pixel" from "it wrote and nothing drew". Those are different answers and
   * only the second is a defect here. */
  unsigned graphics_cycles;
  unsigned graphics_planes_written;

  /* DMA transfers this board has run, and the ones it could not complete
   * because no device is wired to the channel.
   *
   * The second is an instrument, not an error path. Which peripheral sits on
   * which DMA channel is board wiring this machine has not been measured for --
   * `board/ap_dma.h` refuses to claim even the AT's cascade convention, having
   * been wrong once already about the interrupt controllers (`FINDINGS.md`
   * C11) -- so a read or write transfer has no device to move a byte to or
   * from. It is counted rather than refused, and an unwired channel reads the
   * value nothing driving this bus reads: all ones, which is what an empty AT
   * slot already returns on this board. A verify transfer needs no device at
   * all and runs normally, which is what lets a probe measure contention
   * without any of this being settled first. */
  unsigned dma_transfers;
  unsigned dma_unwired_transfers;
  /* Bus ticks in which a controller was asking, and ticks in which one held the
   * bus. A transfer needs both, and which of the two is missing is the whole
   * question when a programmed transfer never runs. */
  unsigned dma_bus_requests;
  unsigned dma_bus_held;
  /* How often the bus tick ran at all. Zero here and zero requests are very
   * different faults and they report identically without this. */
  unsigned bus_ticks;
  /* The physical addresses the last DMA cycle actually used, after translation.
   * A transfer that runs and lands in the wrong place is indistinguishable from
   * one that does not run, from any count. */
  uint32_t dma_last_read;
  uint32_t dma_last_write;
  /* The first distinct addresses a program wrote in the DMA range. Which
   * controller a run programmed is not visible from the registers alone once
   * two decodes are in play -- the addresses are the fact. */
  uint32_t dma_register_writes[12];
  unsigned dma_register_write_count;
  /* The same for the core registers, and for the same reason: which address a
   * program wrote is the fact, and a register that did not change cannot say
   * whether the write missed or the decode did. */
  uint32_t core_register_writes[16];
  unsigned core_register_write_count;
  /* Which controller the cycle in progress is running on. The 8237 hands its
   * callbacks a channel and nothing else -- a `DACK` is all a peripheral sees --
   * so the board keeps the half of the address Table 2-4 needs to name a
   * device: controller 1 channel 3 and controller 2 channel 3 are different
   * lines. */
  unsigned dma_transfer_unit;

  /* The two registers `board/ap_boardreg.h` **declines**: task alias
   * (`010300`) and master request (`011600`). Table 2-8 names both, so the
   * hardware has them; the oracle has neither, and modelling them as all-ones
   * would copy an oracle gap in wearing the clothes of a measurement.
   *
   * Counted separately because the two are in different states of evidence, and
   * a shared counter would hide which. The boot PROMs settle the master request
   * register's *use* -- nine write sites in the DS3500 image, none in either
   * Series 3000 one -- and say nothing about task alias, which appears at no
   * absolute address in any firmware in hand. A run that touches `010300`
   * therefore has something to tell us that no disassembly has; a run that does
   * not is itself the answer to "does this machine's firmware use it".
   *
   * The same reasoning as the translation map's undescribed bytes, and the same
   * fix for the same smaller failure: `ap_boardreg_is_declined` had a test and
   * no caller. */
  unsigned task_alias_reads;
  unsigned task_alias_writes;
  unsigned master_request_reads;
  unsigned master_request_writes;

  /* Accesses to the seven eighths of the translation map's region that no
   * manual describes. `board/ap_atmap.h` has the arithmetic: `017000`-`0177FF`
   * is 2 KB, 128 entries of 16 bits fill 256 bytes of it, and `019411-A00`
   * §4.2.1.4 -- the only source that names the DS3500 -- says nothing about the
   * rest. Neither does `008778-03` §1.2 or Table 2-8, and the web step found
   * nothing; the oracle cannot witness it either, since MAME masks the whole
   * region with `& 0x3ff` and so models 1024 entries where the manual gives
   * 128.
   *
   * So the decode is assumed -- the entries alias every 256 bytes, which is
   * this board's measured idiom everywhere else -- and this counter is what
   * turns an unanswerable question into one that answers itself the moment
   * something real touches the region. Zero here after a boot says the firmware
   * never went there, which is worth as much as a number would be.
   *
   * The alternative was to leave a tested predicate that nothing called, which
   * is what this was: `ap_atmap_decodes_to_entry` existed, had a test, and no
   * caller. */
  unsigned atmap_undescribed_reads;
  unsigned atmap_undescribed_writes;
  uint32_t first_atmap_undescribed_read;
  uint32_t first_atmap_undescribed_write;

  /* Accesses to an AT bus window with no card behind them. Not unmapped: the
   * board decodes the window, so these terminate normally and read `FF`.
   * Counted because "the firmware went looking in an empty slot" is worth
   * knowing, and because a count that suddenly grows is how a card that should
   * have been fitted becomes visible. */
  unsigned atbus_empty_reads;
  unsigned atbus_empty_writes;
  uint32_t first_atbus_empty_read;
  uint32_t first_atbus_empty_write;

  /* Every access, by region, whether or not anything answered. C33's rule taken
   * to its end: "the firmware wanted the calendar" is a question a count of
   * failures cannot answer, because the interesting case is usually a device
   * that *did* answer and was not what the firmware hoped for. A region with
   * zero writes is as informative as one with thousands -- it says the firmware
   * never tried. */
  unsigned region_reads[AP_BOARD_REGIONS];
  unsigned region_writes[AP_BOARD_REGIONS];
} ap_board_t;

/* `start` is the calendar's instant; see `device/ap_mc146818.h` on why it comes
 * from the caller. `node_id` likewise -- a device whose purpose is to be unique
 * per machine must not be identical on every one. */
[[nodiscard]] bool ap_board_init(ap_board_t *board, uint8_t *ram,
                                 uint32_t ram_bytes,
                                 const ap_mc146818_time_t *start,
                                 uint32_t node_id);

/* The same, as a named model. `ap_board_init` is this with the DN3500 -- the
 * reference superset -- so every existing caller keeps the board it had, which
 * is the same shape `ap_machine_init_model` takes and for the same reason. */
[[nodiscard]] bool ap_board_init_model(ap_board_t *board, uint8_t *ram,
                                       uint32_t ram_bytes,
                                       const ap_mc146818_time_t *start,
                                       uint32_t node_id, ap_model_id_t model);

/* Fit the memory array's parity RAM: one bit per byte of main memory, so
 * `bytes` must be at least `(ram_bytes + 7) / 8`.
 *
 * Caller-supplied like the RAM itself and the frame buffers, because
 * `src/core` allocates nothing. A board without it is one with no parity
 * circuitry fitted -- see `board/ap_parity.h` on why that is a describable
 * machine and how a run says so. */
[[nodiscard]] bool ap_board_attach_parity(ap_board_t *board, uint8_t *bad,
                                          uint32_t bytes);

/* Attach a boot PROM image. Fails if it is larger than the region Table 2-8
 * gives it -- an image that does not fit is not this machine's PROM, and
 * truncating it would run whatever happened to be in the first 64 KB. */
[[nodiscard]] bool ap_board_load_prom(ap_board_t *board, const uint8_t *prom,
                                      uint32_t bytes);

/* The reset vector the PROM carries: `[030]` takes the initial supervisor stack
 * pointer from address 0 and the initial program counter from address 4, both
 * big-endian long words. False if no PROM is loaded. */
[[nodiscard]] bool ap_board_reset_vector(const ap_board_t *board,
                                         uint32_t *stack, uint32_t *pc);

/* Which part of *this board's* machine an address belongs to. Takes the board
 * because the answer is a model's, not the architecture's: a DS3000 puts its
 * serial ports at `008400` where a DS3500 puts them at `010400`. */
[[nodiscard]] ap_board_region_t ap_board_region(const ap_board_t *board,
                                                uint32_t address);
[[nodiscard]] const char *ap_board_region_name(ap_board_region_t region);

/* How long this address takes to answer, in `AP_TIME_BASE_HZ` units. Zero means
 * no document gives a figure, and the caller should charge its minimum.
 *
 * ## Decided by address, not by device
 *
 * Table 2-8 puts the AT-compatible bus in two address windows, and a card in
 * either answers over that bus whatever it is. So the figure follows the
 * *address*: the disk, the tape, the floppy and any empty slot are all inside
 * the I/O window and all get the I/O cycle without this having to decide, one
 * device at a time, which of them is an AT card. `board/ap_atbus.h` has the
 * figures and where they come from.
 *
 * The one consequence to keep an eye on is the display: both graphics memories
 * decode inside the AT *memory* window, so a frame buffer access is charged an
 * AT memory cycle. If the controller turns out to sit on a local bus instead,
 * that is too slow by a large factor -- recorded in `PROJECT_STATUS.md` rather
 * than guessed at either way, and visible because the region counters separate
 * graphics from the rest.
 *
 * ## Zero for everything else, and that is a statement
 *
 * Main memory, the boot PROM and the board's own registers at `010000`-`017000`
 * are not on the AT bus and `008778-03` publishes no cycle time for them. They
 * answer at the minimum, which is what this core did everywhere before there
 * was a figure anywhere -- not because they are known to be that fast. */
[[nodiscard]] ap_time_t ap_board_access_time(const ap_board_t *board,
                                             uint32_t address, bool read);

/* Sample every device's interrupt output onto the controllers' request lines.
 *
 * ## Why this is a call and not something a device does when it changes
 *
 * The lines are *levels*, not events: `[8259]`'s IRR follows its pins, and a
 * device whose condition has gone away stops requesting whether or not anyone
 * told the controller. Sampling them all in one place is the truthful shape,
 * and it is what a wire does.
 *
 * It is also a call rather than an implicit part of every access because the
 * machine is what owns the clock. This is the first piece of the tick loop to
 * involve a device at all, and it stays a piece: nothing here advances a
 * counter, so an interrupt appears only when something a program did produced
 * one -- writing a DUART register, unmasking a timer output. That is enough for
 * a probe to raise an interrupt on demand without any time passing, which is
 * exactly what the ordering verification needs and could not have before.
 *
 * ## The lines
 *
 * Each device's module carries its own line number and its own IRQ accessor,
 * and several of those headers say in as many words that "the board does the
 * wiring". The board did not: every accessor and every constant existed, was
 * tested, and was joined to nothing -- the same shape as a decoder the step
 * never asked, or a model clock nothing read. The disk has two lines and no
 * accessor yet, so it is absent here rather than wired to a guess. */
void ap_board_sample_interrupts(ap_board_t *board);

/* The 68030 interrupt level the board is asserting, or zero for none.
 *
 * `AP_INTR_CPU_LEVEL` when the master controller has something, which is a
 * *measured* figure and not a manual's -- `FINDINGS.md` C12 swept the CPU mask
 * to find it. Zero otherwise: level zero means no interrupt on this part. */
[[nodiscard]] unsigned ap_board_interrupt_level(const ap_board_t *board);

/* Level 7, the only interrupt on this board that does not come from an 8259.
 * `008778-03` §3.2 names the level, and §3.2 again the autovector -- see
 * `board/ap_parity.h`, which has the passage. */
#define AP_BOARD_PARITY_LEVEL 7u

/* Whether the memory array is holding a parity error interrupt up. */
[[nodiscard]] bool ap_board_parity_interrupt(const ap_board_t *board);

/* Run the acknowledge cycle and answer the vector the controllers supply.
 *
 * The Apollo scheme is vectored, not autovectored: the two controllers carry
 * vector bases `A0` and `A8` so the sixteen levels occupy `A0`-`AF`. A caller
 * that autovectored would land on vector 24 + level and be wrong by a hundred
 * and something. */
[[nodiscard]] uint8_t ap_board_interrupt_acknowledge(ap_board_t *board);

/* ---------------------------------------------------------------------------
 * The bus, and who is holding it
 *
 * One arbitration point, one priority order, and the processor at the bottom of
 * it -- `[030]` §7.7: "the bus controller in the MC68030 manages the bus
 * arbitration signals so that the processor has the lowest priority". A DMA
 * controller with a channel to service asks for the bus, wins it, and runs
 * transfers until it lets go. The processor does not run in the meantime, and
 * that is contention: nothing here computes a delay or charges a penalty.
 * ------------------------------------------------------------------------- */

/* One bus clock. Drives each controller's request into the arbiter, advances
 * the arbitration, and runs a transfer if a controller is holding the bus.
 *
 * ## Each controller asks once, because it has one request output
 *
 * An 8237A has a single HRQ, so a controller with any channel asking makes one
 * request however many are. Which of the arbiter's eight lines each controller
 * appears on is the AT's DRQ0-3 / DRQ4-7 split; this board has not been
 * measured for it, and what it decides is only which of the two controllers
 * wins a simultaneous request. Recorded rather than hidden -- see
 * `PROJECT_STATUS.md` -- and nothing yet requests from both.
 */
void ap_board_bus_tick(ap_board_t *board);

/* Whether the processor may run a cycle this clock. False while a controller
 * holds the bus, which is the whole of how contention reaches the CPU. */
[[nodiscard]] bool ap_board_processor_may_run(const ap_board_t *board);

/* Whether the board asserts `CIIN` for this address -- that is, whether it is a
 * *device* rather than memory.
 *
 * ## Why a board has to say this at all
 *
 * The 68030's caches are the processor's, and nothing in the processor knows
 * which addresses are registers. `[030]` §6.1.3 gives the hardware the job:
 * "the cache inhibit in (CIIN) signal ... allows the system to inhibit caching
 * on a cycle-by-cycle basis", and a board decodes its own I/O space to drive it.
 *
 * Without it every device register is cacheable, and the consequence is not
 * subtle: a firmware polling a status register reads it once from the bus and
 * then forever out of the cache, so a bit that changes in the device never
 * changes for the program. The boot PROM's console loop did exactly that --
 * 15,721 executions of a poll that reached the serial port **twice**, spinning
 * on a status it could no longer see move.
 *
 * ## Memory is cacheable and everything else is not
 *
 * Main memory and the boot PROM, and nothing else. That is broader than naming
 * each device individually and it is the safer direction: a region this board
 * has not modelled yet is not memory, so it defaults to uncacheable rather than
 * to a cached register nobody notices.
 *
 * **Decided on the address the caches use**, which is the logical one. With
 * translation off -- which is how the firmware runs at this point -- it is the
 * physical address the board would decode, so the two agree. Under a live MMU
 * the real machine drives `CIOUT` from the descriptor's `CI` bit instead, and
 * that is the mechanism a caller should use there; this is the board's own
 * decode and cannot see a translation it was not shown. */
[[nodiscard]] bool ap_board_cache_inhibited(const ap_board_t *board,
                                            uint32_t address);

/* ---------------------------------------------------------------------------
 * Time
 *
 * `CLAUDE.md` opens with "one `tick()` per machine cycle, every subsystem
 * advancing inside it". This is that, for the devices that keep time: the
 * interval timer and the calendar, each advanced to the machine's absolute
 * `now`. Until it existed nothing in this core advanced on its own, so a
 * counter reached terminal count only if a test reached in and advanced it, and
 * four separate verifications were waiting on that.
 *
 * ## Absolute time, which is what makes the call rate not matter
 *
 * Every device here takes an absolute instant and carries its own remainder --
 * `ap_timer_advance` issues one pulse per elapsed period of each timer's own
 * rate, `ap_mc146818_advance` one update per second -- so advancing once per
 * instruction reaches exactly the state advancing once per clock would. The
 * device is a function of the instant, not of how often it was asked.
 *
 * What *is* quantised is the moment a change is noticed: an interrupt raised
 * partway through an instruction is seen at the end of it. That is a documented
 * approximation with a stated cost -- at 25 MHz an instruction is a handful of
 * clocks, and the timers' fastest rate is 250 kHz, a hundred times slower --
 * and it is bounded by the longest instruction rather than unbounded.
 *
 * ## The DUART's counter is not here
 *
 * `device/ap_mc68681.h` has no advance function: its counter/timer is modelled
 * as registers and commands and nothing drives it. That is the memory refresh's
 * item, and adding a call here for a device that cannot use it would be the
 * pretence of a tick loop rather than one. */
void ap_board_advance(ap_board_t *board, ap_time_t now);

/* Read or write one byte. `ok` reports whether anything answered; an unmapped
 * access is counted and reported rather than quietly returning zero. */
[[nodiscard]] uint8_t ap_board_read(ap_board_t *board, uint32_t address,
                                    bool *ok);
void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok);

/* A write of `count` bytes as the *processor* issued it, big-endian, with the
 * most significant byte at `address`.
 *
 * Almost every region on this board is eight bits wide and this is the byte
 * loop. The **graphics memory** is not: the display controller is sixteen bits
 * and a CPU access to its image memory is a blit *cycle* with a byte mask, not
 * one or two stores. Splitting a word write into two byte writes there would
 * run two half-masked blits where the hardware runs one, and in the two-cycle
 * modes it would advance the controller's cycle counter twice -- so the second
 * write of a pair would complete the blit the first had only begun, and every
 * subsequent access would be out of phase.
 *
 * The width was known at the machine and thrown away at this boundary, which is
 * why it is passed now. `count` is 1, 2 or 4; anything else is refused rather
 * than looped, because a size this bus cannot issue is a caller's mistake. */
[[nodiscard]] bool ap_board_write_access(ap_board_t *board, uint32_t address,
                                         unsigned count, uint32_t value);

/* The read side of the same, and it matters for the same region and one more
 * reason: a read of the display controller's image memory is a *cycle*. It
 * comes from the source plane rather than plane 0, and in most modes it latches
 * while reading -- so splitting a word read into two byte reads would latch
 * twice and leave the guard latch holding a byte pair rather than a word. */
[[nodiscard]] bool ap_board_read_access(ap_board_t *board, uint32_t address,
                                        unsigned count, uint32_t *out);

/* Press or release a keyboard key, delivering its scan code to serial 1
 * channel A -- the port the keyboard is wired to, confirmed from both the
 * oracle's machine configuration and the boot PROM's own poll loop.
 *
 * Answers false when there was no transition to report: a repeated press, a
 * release of a key that was not down, or a key outside the matrix.
 *
 * **The link's rate is assumed, not measured.** The code is delivered at the
 * port's own current clock select, which models a correctly configured link and
 * makes the DUART's rate check vacuous for this path. The real keyboard's line
 * rate is not known -- the firmware configures channel *B* in the traces we
 * have and leaves channel A at reset -- so asserting a figure here would be
 * inventing one. Recorded rather than guessed. */
[[nodiscard]] bool ap_board_key_press(ap_board_t *board, unsigned key);
[[nodiscard]] bool ap_board_key_release(ap_board_t *board, unsigned key);

#endif /* APOLLO_BOARD_AP_BOARD_H */
