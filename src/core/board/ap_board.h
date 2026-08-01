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

#include "board/ap_atmap.h"
#include "board/ap_boardreg.h"
#include "board/ap_calendar.h"
#include "board/ap_disk.h"
#include "board/ap_dma.h"
#include "board/ap_intr.h"
#include "board/ap_nodeid.h"
#include "board/ap_sio.h"
#include "board/ap_graphics.h"
#include "board/ap_tape.h"
#include "board/ap_timer.h"

/* `008778-03` Table 2-8. */
#define AP_BOARD_PROM_BASE 0x000000u
#define AP_BOARD_PROM_SIZE 0x010000u
#define AP_BOARD_RAM_BASE 0x1000000u

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
  AP_BOARD_REGION_RAM,
} ap_board_region_t;

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

  /* Writes to a read-only memory, which are absorbed rather than refused and so
   * are *not* unmapped. Kept apart because the two mean opposite things: an
   * unmapped write is an address nothing answers, while this is an address
   * something answers and cannot store. Folding them together would both hide a
   * driver writing to a PROM and make a harmless write look like a fault. */
  unsigned rom_writes;
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

/* Read or write one byte. `ok` reports whether anything answered; an unmapped
 * access is counted and reported rather than quietly returning zero. */
[[nodiscard]] uint8_t ap_board_read(ap_board_t *board, uint32_t address,
                                    bool *ok);
void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok);

#endif /* APOLLO_BOARD_AP_BOARD_H */
