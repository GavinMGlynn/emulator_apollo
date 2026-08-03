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

#include "board/ap_atbus.h"
#include "board/ap_atmap.h"
#include "board/ap_boardreg.h"
#include "board/ap_calendar.h"
#include "board/ap_disk.h"
#include "board/ap_dma.h"
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

typedef struct ap_board {
  ap_boardreg_t registers;
  ap_atmap_t translation_map;
  ap_intr_t interrupts;
  ap_timer_t timer;
  ap_calendar_t calendar;
  ap_dma_t dma;
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

[[nodiscard]] ap_board_region_t ap_board_region(uint32_t address);
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

/* Read or write one byte. `ok` reports whether anything answered; an unmapped
 * access is counted and reported rather than quietly returning zero. */
[[nodiscard]] uint8_t ap_board_read(ap_board_t *board, uint32_t address,
                                    bool *ok);
void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok);

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
