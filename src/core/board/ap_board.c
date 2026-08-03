#include "board/ap_board.h"

#include <string.h>

static bool in(uint32_t a, uint32_t base, uint32_t size) {
  return a >= base && a < base + size;
}

ap_board_region_t ap_board_region(uint32_t address) {
  /* Table 2-8, in its own order. Each base and size is the one the device's own
   * module carries, so a placement corrected there cannot drift from here. */
  if (in(address, AP_BOARD_PROM_BASE, AP_BOARD_PROM_SIZE)) {
    return AP_BOARD_REGION_PROM;
  }
  /* Four contiguous registers from `010000`, and two more that are *not*
   * adjacent to them or to each other. `ap_boardreg.h` has carried all six
   * since it was written; only the first four were routed here, so the latch
   * page and master request registers existed and were unreachable.
   *
   * That is the failure mode a contiguous range invites: it looks like it
   * covers a device, and it silently covers only the part that happens to be
   * contiguous. The boot PROM's `CLR.B $00011600` bus errored on every pass
   * through its reset path, and each fault drained another frame off a 384-byte
   * supervisor stack until it ran out. */
  if (in(address, AP_BOARDREG_CPU_STATUS_ADDR, 4u * AP_BOARDREG_RANGE) ||
      in(address, AP_BOARDREG_LATCH_PAGE_ADDR, AP_BOARDREG_RANGE) ||
      in(address, AP_BOARDREG_MASTER_REQUEST_ADDR, AP_BOARDREG_RANGE)) {
    return AP_BOARD_REGION_CORE_REGISTER;
  }
  if (in(address, AP_SIO1_ADDR, 2u * AP_SIO_RANGE)) {
    return AP_BOARD_REGION_SIO;
  }
  if (in(address, AP_TIMER_ADDR, AP_TIMER_RANGE)) {
    return AP_BOARD_REGION_TIMER;
  }
  if (in(address, AP_CALENDAR_ADDR, AP_CALENDAR_RANGE)) {
    return AP_BOARD_REGION_CALENDAR;
  }
  if (in(address, AP_DMA1_ADDR, 2u * AP_DMA_RANGE)) {
    return AP_BOARD_REGION_DMA;
  }
  if (in(address, AP_INTR_MASTER_ADDR, 2u * AP_INTR_RANGE)) {
    return AP_BOARD_REGION_INTERRUPT;
  }
  if (in(address, AP_NODEID_ADDR, AP_NODEID_RANGE)) {
    return AP_BOARD_REGION_NODE_ID;
  }
  if (in(address, AP_ATMAP_BASE, AP_ATMAP_LIMIT - AP_ATMAP_BASE + 1u)) {
    return AP_BOARD_REGION_TRANSLATION_MAP;
  }
  if (in(address, AP_DISK_FIXED_ADDR, AP_DISK_FIXED_SIZE) ||
      in(address, AP_DISK_FLOPPY_ADDR, AP_DISK_FLOPPY_SIZE)) {
    return AP_BOARD_REGION_DISK;
  }
  if (in(address, AP_TAPE_ADDR, AP_TAPE_RANGE)) {
    return AP_BOARD_REGION_TAPE;
  }
  {
    bool colour = false;
    uint32_t offset = 0;
    /* Both, and both before the AT bus windows below: the graphics memories sit
     * inside the AT memory window, so a window checked first would report the
     * machine's own frame buffer as an empty expansion slot. */
    if (ap_graphics_decode(address, &colour, &offset) ||
        ap_graphics_decode_memory(address, &colour, &offset)) {
      return AP_BOARD_REGION_GRAPHICS;
    }
  }
  if (address >= AP_BOARD_RAM_BASE && address <= AP_BOARD_RAM_LIMIT) {
    /* The space allocated to memory, not the memory fitted. An address in here
     * with no SIMM behind it is still a main memory address -- the read path
     * bounds-checks against what is actually present and reports it unmapped --
     * which is the same distinction the AT bus windows make between an empty
     * slot and an address nothing decodes. */
    return AP_BOARD_REGION_RAM;
  }
  /* Last, so every device *inside* a window keeps its own region. The tape,
   * the disk and the display controller all sit within the AT I/O window and
   * are matched above; what reaches here is window with nothing behind it. */
  if ((address >= AP_BOARD_ATBUS_IO_BASE &&
       address <= AP_BOARD_ATBUS_IO_END) ||
      (address >= AP_BOARD_ATBUS_MEMORY_BASE &&
       address <= AP_BOARD_ATBUS_MEMORY_END)) {
    return AP_BOARD_REGION_ATBUS;
  }
  return AP_BOARD_REGION_UNMAPPED;
}

ap_time_t ap_board_access_time(const ap_board_t *board, uint32_t address,
                               bool read) {
  const ap_atbus_timing_t *timing = ap_atbus_timing(board->at_bus_series);

  /* By address, not by device: a card in either window answers over the AT bus
   * whatever it is, so the disk, the tape, the floppy, the display's memories
   * and an empty slot all take the same figure without this deciding, one
   * device at a time, which of them is an AT card.
   *
   * Eight bits wide because that is what a card gets when it does not assert
   * `MEM_CS16.L` or `IO_CS16.L` -- the AT's default rather than a choice made
   * here. A card that asserts one is faster, and nothing on this board is known
   * to. */
  if (in(address, AP_BOARD_ATBUS_IO_BASE,
         AP_BOARD_ATBUS_IO_END - AP_BOARD_ATBUS_IO_BASE + 1u)) {
    return ap_atbus_access_time(timing, AP_ATBUS_CYCLE_IO_8, read);
  }
  if (in(address, AP_BOARD_ATBUS_MEMORY_BASE,
         AP_BOARD_ATBUS_MEMORY_END - AP_BOARD_ATBUS_MEMORY_BASE + 1u)) {
    return ap_atbus_access_time(timing, AP_ATBUS_CYCLE_MEMORY, read);
  }

  /* "Zero for everything else, and that is a statement." Main memory, the PROM
   * and the board's own registers have no published cycle time; they answer at
   * the caller's minimum because nothing says otherwise, not because they are
   * known to be that fast. */
  return 0u;
}

/* The two registers Table 2-8 names and `ap_boardreg` declines. Counted here
 * rather than in the register module because it is the *machine* that was
 * watched, and a count is our record of watching rather than state the board
 * has -- which is why these stay out of the hash, like every other counter. */
static void count_declined(ap_board_t *board, uint32_t address, bool read) {
  if (!ap_boardreg_is_declined(address)) {
    return;
  }
  const bool task_alias =
      address >= AP_BOARDREG_TASK_ALIAS_ADDR &&
      address < AP_BOARDREG_TASK_ALIAS_ADDR + AP_BOARDREG_RANGE;
  if (task_alias) {
    if (read) {
      board->task_alias_reads++;
    } else {
      board->task_alias_writes++;
    }
    return;
  }
  if (read) {
    board->master_request_reads++;
  } else {
    board->master_request_writes++;
  }
}

void ap_board_sample_interrupts(ap_board_t *board) {
  /* One line per device that has one, each from the device's own accessor and
   * its own line constant, so a corrected placement cannot drift from here.
   *
   * Levels, sampled: a device that has stopped requesting clears its line here
   * without anyone having to notice the moment it stopped. */
  ap_intr_set_request(&board->interrupts, AP_TIMER_IRQ,
                      ap_timer_irq(&board->timer));
  ap_intr_set_request(&board->interrupts, AP_SIO_IRQ,
                      ap_sio_irq(&board->sio));
  ap_intr_set_request(&board->interrupts, AP_CALENDAR_IRQ,
                      ap_calendar_irq(&board->calendar));
  ap_intr_set_request(&board->interrupts, AP_TAPE_IRQ,
                      ap_tape_irq(&board->tape));
  /* The disk's two lines -- `AP_DISK_FIXED_IRQ` and `AP_DISK_FLOPPY_IRQ` -- are
   * deliberately absent: `board/ap_disk.h` declares the constants and no IRQ
   * accessor, so wiring them would mean inventing the condition that raises
   * them. It lands with the controller's own item. */
}

unsigned ap_board_interrupt_level(const ap_board_t *board) {
  /* Measured, not transcribed: `FINDINGS.md` C12 started the interval timer by
   * hand and swept the CPU's mask -- taken at 5, blocked at 6. Zero is "no
   * interrupt", which is what level zero means on this part. */
  return ap_intr_pending(&board->interrupts) ? AP_INTR_CPU_LEVEL : 0u;
}

uint8_t ap_board_interrupt_acknowledge(ap_board_t *board) {
  return ap_intr_acknowledge(&board->interrupts);
}

/* One line, because after the cascade there is one request output that reaches
 * the processor. `008778-03` §2.4: "DRQ4 is used on the system board to cascade
 * Channels 0 through 3 ... It is not available on the AT-compatible bus." So
 * controller 1's request arrives on controller 2's channel 0, and only
 * controller 2 asks the arbiter.
 *
 * The arbiter line is DRQ0's, the highest, because after the cascade the
 * request standing there is whichever channel won both encoders -- there is no
 * second DMA claimant for it to be ordered against. */
#define DMA_ARBITER_LINE 0u

/* The memory side of a transfer. The part drives sixteen bits and the map
 * supplies the rest -- `019411-A00` §4.2.1.4, and the reason a DMA address is
 * not a physical one on this machine at all. */
static uint32_t dma_physical(const ap_board_t *board, uint16_t dma_address) {
  /* 8-bit, because that is what a channel programmed for byte transfers is and
   * nothing here yet programs a 16-bit one. The map indexes differently for the
   * two widths, which `ap_atmap.h` has and this passes through rather than
   * deciding. */
  return ap_atmap_translate(&board->translation_map, dma_address,
                            AP_ATMAP_TRANSFER_8BIT);
}

static uint8_t dma_memory_read(void *context, uint16_t address) {
  ap_board_t *board = (ap_board_t *)context;
  const uint32_t physical = dma_physical(board, address);
  bool ok = false;
  return ap_board_read(board, physical, &ok);
}

static void dma_memory_write(void *context, uint16_t address, uint8_t value) {
  ap_board_t *board = (ap_board_t *)context;
  const uint32_t physical = dma_physical(board, address);
  bool ok = false;
  ap_board_write(board, physical, value, &ok);
}

/* The peripheral side, which no device is wired to. Counted, and answering what
 * nothing driving this bus answers -- all ones, the same value an empty AT slot
 * already reads here. See `ap_board.h`. */
static uint8_t dma_device_read(void *context, unsigned channel) {
  (void)channel;
  ((ap_board_t *)context)->dma_unwired_transfers++;
  return 0xFFu;
}

static void dma_device_write(void *context, unsigned channel, uint8_t value) {
  (void)channel;
  (void)value;
  ((ap_board_t *)context)->dma_unwired_transfers++;
}

void ap_board_bus_tick(ap_board_t *board) {
  const ap_i8237_bus_t bus = {
      .memory_read = dma_memory_read,
      .memory_write = dma_memory_write,
      .device_read = dma_device_read,
      .device_write = dma_device_write,
      .context = board,
  };

  /* The cascade, wired rather than encoded. Controller 1 has one HRQ however
   * many of its channels are asking, and it lands on controller 2's channel 0 --
   * which is that controller's *highest* priority. That is why Table 2-4's
   * priority column runs 1-4 for DRQ0-3 and 5-7 for DRQ5-7 instead of following
   * the line numbers: channels 0 through 3 outrank 5 through 7 because they
   * arrive through the cascade. The order is a consequence here, not a table. */
  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_CASCADE_UNIT],
                           AP_DMA_CASCADE_CHANNEL,
                           ap_i8237_service_pending(&board->dma.controller[0]) >=
                               0);

  const int selected =
      ap_i8237_service_pending(&board->dma.controller[AP_DMA_CASCADE_UNIT]);
  ap_arbiter_request(&board->arbiter, DMA_ARBITER_LINE, selected >= 0);

  /* The arbitration resolves first, and the master then uses the bus it has
   * just been given. The other order costs a clock at every handover and, worse,
   * makes the *first* clock of mastership do nothing -- which reads as a real
   * arbitration cost and is entirely this function's ordering. */
  ap_arbiter_tick(&board->arbiter);

  /* A transfer runs only while its controller *holds* the bus -- not while it
   * is merely asking, and not while the grant is offered and unacknowledged.
   * §7.7.3 puts real time between those, and collapsing them would make every
   * handover free and delete the contention this exists to produce. */
  if (ap_arbiter_master(&board->arbiter) != (int)DMA_ARBITER_LINE) {
    return;
  }

  /* Whose transfer it is follows from which channel the second controller
   * selected. Its channel 0 in cascade mode is not a transfer at all -- the
   * part refuses one, and correctly -- it is the first controller's turn. */
  const unsigned unit =
      (selected == (int)AP_DMA_CASCADE_CHANNEL &&
       ap_i8237_mode_of(&board->dma.controller[AP_DMA_CASCADE_UNIT],
                        AP_DMA_CASCADE_CHANNEL) == AP_I8237_MODE_CASCADE)
          ? 0u
          : AP_DMA_CASCADE_UNIT;

  if (ap_i8237_transfer(&board->dma.controller[unit], &bus).ran) {
    board->dma_transfers++;
  }
}

void ap_board_advance(ap_board_t *board, ap_time_t now) {
  /* Each to the same instant, and each carrying its own remainder. Order does
   * not matter and must not: two devices advanced to the same absolute time
   * cannot influence each other through the advance itself, which is what makes
   * this a tick rather than a schedule. */
  ap_timer_advance(&board->timer, now);
  ap_calendar_advance(&board->calendar, now);
}

bool ap_board_processor_may_run(const ap_board_t *board) {
  return ap_arbiter_processor_may_run(&board->arbiter);
}

const char *ap_board_region_name(ap_board_region_t region) {
  switch (region) {
  case AP_BOARD_REGION_UNMAPPED: return "unmapped";
  case AP_BOARD_REGION_PROM: return "boot PROM";
  case AP_BOARD_REGION_CORE_REGISTER: return "core register";
  case AP_BOARD_REGION_SIO: return "serial";
  case AP_BOARD_REGION_TIMER: return "interval timer";
  case AP_BOARD_REGION_CALENDAR: return "calendar";
  case AP_BOARD_REGION_DMA: return "DMA";
  case AP_BOARD_REGION_INTERRUPT: return "interrupt controller";
  case AP_BOARD_REGION_NODE_ID: return "node ID PROM";
  case AP_BOARD_REGION_TRANSLATION_MAP: return "translation map";
  case AP_BOARD_REGION_DISK: return "disk/floppy";
  case AP_BOARD_REGION_TAPE: return "cartridge tape";
  case AP_BOARD_REGION_GRAPHICS: return "display controller";
  case AP_BOARD_REGION_ATBUS: return "AT bus (empty slot)";
  case AP_BOARD_REGION_RAM: return "main memory";
  }
  return "unmapped";
}

bool ap_board_init(ap_board_t *board, uint8_t *ram, uint32_t ram_bytes,
                   const ap_mc146818_time_t *start, uint32_t node_id) {
  memset(board, 0, sizeof *board);
  ap_boardreg_init(&board->registers);
  ap_atmap_init(&board->translation_map);
  ap_intr_reset(&board->interrupts);
  if (!ap_timer_reset(&board->timer)) {
    return false;
  }
  if (!ap_calendar_reset(&board->calendar, start)) {
    return false;
  }
  ap_dma_reset(&board->dma);
  ap_sio_reset(&board->sio);
  ap_nodeid_init(&board->node_id, node_id);
  ap_disk_reset(&board->disk);
  ap_tape_reset(&board->tape);
  /* No display controller fitted by default. The blocks still decode -- a
   * DN3500 answers there whether or not a screen is present -- and the ID
   * register reads `FF`, which is how the firmware learns there is none. */
  ap_graphics_init(&board->graphics, AP_SCREEN_NONE);
  ap_kbd_reset(&board->keyboard);
  /* `PROVISIONAL`, and the field's comment says why. Set explicitly rather than
   * left to the `memset` above being zero: the value is a claim about this
   * board, and one that happens to be enumerator zero is still a claim. */
  ap_arbiter_reset(&board->arbiter);
  board->at_bus_series = AP_ATBUS_SERIES_3000;
  board->ram = ram;
  board->ram_bytes = ram_bytes;
  return true;
}

uint8_t ap_board_read(ap_board_t *board, uint32_t address, bool *ok) {
  *ok = true;
  const ap_board_region_t counted = ap_board_region(address);
  if ((unsigned)counted < AP_BOARD_REGIONS) {
    board->region_reads[counted]++;
  }
  switch (counted) {
  case AP_BOARD_REGION_CORE_REGISTER:
    count_declined(board, address, true);
    return ap_boardreg_read8(&board->registers, address);
  case AP_BOARD_REGION_SIO:
    return ap_sio_read(&board->sio, address);
  case AP_BOARD_REGION_TIMER:
    return ap_timer_read(&board->timer, address);
  case AP_BOARD_REGION_CALENDAR:
    return ap_calendar_read(&board->calendar, address);
  case AP_BOARD_REGION_DMA:
    return ap_dma_read(&board->dma, address);
  case AP_BOARD_REGION_INTERRUPT:
    return ap_intr_read(&board->interrupts, address);
  case AP_BOARD_REGION_NODE_ID:
    return ap_nodeid_read(&board->node_id, address);
  case AP_BOARD_REGION_TRANSLATION_MAP: {
    if (!ap_atmap_decodes_to_entry(address)) {
      if (board->atmap_undescribed_reads == 0u) {
        board->first_atmap_undescribed_read = address;
      }
      board->atmap_undescribed_reads++;
    }
    /* A map entry is sixteen bits and this path is a byte, so the byte lane
     * has to be chosen -- big-endian, so the even address is the *high* half.
     * Returning the low half for both, which this did, makes every entry read
     * back as its own low byte duplicated. */
    const uint16_t entry = ap_atmap_read(&board->translation_map, address);
    return (address & 1u) != 0u ? (uint8_t)(entry & 0xFFu)
                                : (uint8_t)(entry >> 8);
  }
  case AP_BOARD_REGION_DISK:
    return ap_disk_read(&board->disk, address);
  case AP_BOARD_REGION_TAPE:
    return ap_tape_read(&board->tape, address);
  case AP_BOARD_REGION_GRAPHICS:
    return ap_graphics_read(&board->graphics, address);
  case AP_BOARD_REGION_ATBUS:
    /* The window decodes and nothing drives the data lines, so the pull-ups
     * answer. `FF` rather than unmapped: the cycle terminates normally on the
     * real machine, and reporting a fault here would crash an expansion ROM
     * scan that is supposed to simply find nothing. */
    if (board->atbus_empty_reads == 0u) {
      board->first_atbus_empty_read = address;
    }
    board->atbus_empty_reads++;
    return 0xFFu;
  case AP_BOARD_REGION_RAM: {
    uint32_t offset = address - AP_BOARD_RAM_BASE;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break; /* past the memory actually fitted */
    }
    return board->ram[offset];
  }
  case AP_BOARD_REGION_PROM: {
    uint32_t offset = address - AP_BOARD_PROM_BASE;
    if (board->prom == NULL || offset >= board->prom_bytes) {
      /* No PROM, or past the image. Reported as unmapped rather than answered
       * with zero, because a machine answering the PROM with zeros looks like
       * one with a blank PROM rather than one without a PROM at all. */
      break;
    }
    return board->prom[offset];
  }
  case AP_BOARD_REGION_UNMAPPED:
    break;
  }
  *ok = false;
  if (board->unmapped_reads == 0u) {
    board->first_unmapped_read = address;
  }
  board->unmapped_reads++;
  return 0xFFu;
}

void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok) {
  *ok = true;
  const ap_board_region_t counted = ap_board_region(address);
  if ((unsigned)counted < AP_BOARD_REGIONS) {
    board->region_writes[counted]++;
  }
  switch (counted) {
  case AP_BOARD_REGION_CORE_REGISTER:
    count_declined(board, address, false);
    ap_boardreg_write8(&board->registers, address, value);
    return;
  case AP_BOARD_REGION_SIO:
    ap_sio_write(&board->sio, address, value);
    return;
  case AP_BOARD_REGION_TIMER:
    ap_timer_write(&board->timer, address, value);
    return;
  case AP_BOARD_REGION_CALENDAR:
    ap_calendar_write(&board->calendar, address, value);
    return;
  case AP_BOARD_REGION_DMA:
    ap_dma_write(&board->dma, address, value);
    return;
  case AP_BOARD_REGION_INTERRUPT:
    ap_intr_write(&board->interrupts, address, value);
    return;
  case AP_BOARD_REGION_TRANSLATION_MAP: {
    if (!ap_atmap_decodes_to_entry(address)) {
      if (board->atmap_undescribed_writes == 0u) {
        board->first_atmap_undescribed_write = address;
      }
      board->atmap_undescribed_writes++;
    }
    /* Read-modify-write of the addressed half, for the reason the read gives.
     * Writing the byte as the whole entry -- which this did -- means a program
     * setting an entry the only way a 68030 can, two byte cycles, ends with the
     * *second* byte in both halves. Every page number above `00FF` was silently
     * truncated, so a DMA transfer aimed at main memory at `01000000` landed in
     * the boot PROM at zero. Found by a transfer that did not arrive, not by
     * reading this code. */
    const uint16_t held = ap_atmap_read(&board->translation_map, address);
    const uint16_t entry =
        (address & 1u) != 0u
            ? (uint16_t)((held & 0xFF00u) | (uint16_t)value)
            : (uint16_t)((held & 0x00FFu) | ((unsigned)value << 8u));
    ap_atmap_write(&board->translation_map, address, entry);
    return;
  }
  case AP_BOARD_REGION_DISK:
    ap_disk_write(&board->disk, address, value);
    return;
  case AP_BOARD_REGION_TAPE:
    ap_tape_write(&board->tape, address, value);
    return;
  case AP_BOARD_REGION_GRAPHICS:
    ap_graphics_write(&board->graphics, address, value);
    return;
  case AP_BOARD_REGION_ATBUS:
    if (board->atbus_empty_writes == 0u) {
      board->first_atbus_empty_write = address;
    }
    board->atbus_empty_writes++;
    return;
  case AP_BOARD_REGION_RAM: {
    uint32_t offset = address - AP_BOARD_RAM_BASE;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break;
    }
    board->ram[offset] = value;
    return;
  }
  case AP_BOARD_REGION_PROM:
    if (board->prom == NULL) {
      /* No PROM fitted, so nothing decodes the address and the write is
       * unmapped like any other. The read path already reports the region
       * absent in this case, and the two directions have to agree: a board
       * where a missing PROM refuses reads but absorbs writes describes no
       * hardware. */
      break;
    }
    [[fallthrough]];
  case AP_BOARD_REGION_NODE_ID:
    /* Both are read-only memories, and a write to one is **absorbed**, not
     * refused. Something is decoding the address and terminating the cycle --
     * the storage simply cannot change -- so the processor sees an ordinary
     * completed write and no bus error.
     *
     * The oracle settles this rather than intuition: MAME's DN3500 maps the
     * boot ROM region for write as well as read, to a handler that does
     * nothing but log. It also carries this, naming the very image we boot:
     *
     *     if (pc == 0x00002c1c && address == 0x00000004 ...)
     *         // don't log invalid code in 3500_boot_12191_7.bin
     *
     * So this firmware really does write to its own boot ROM, and the hardware
     * really does shrug. A board that bus-errored here would fault real
     * firmware doing a thing the real machine tolerates.
     *
     * Counted separately all the same. "The firmware wrote to a PROM" stays
     * worth knowing even though it is not an error, and folding it into the
     * unmapped total would hide it among addresses nothing decodes at all. */
    if (board->rom_writes == 0u) {
      board->first_rom_write = address;
    }
    board->rom_writes++;
    *ok = true;
    return;
  case AP_BOARD_REGION_UNMAPPED:
    break;
  }
  *ok = false;
  if (board->unmapped_writes == 0u) {
    board->first_unmapped_write = address;
  }
  board->unmapped_writes++;
}

bool ap_board_load_prom(ap_board_t *board, const uint8_t *prom,
                        uint32_t bytes) {
  if (prom == NULL || bytes == 0u || bytes > AP_BOARD_PROM_SIZE) {
    return false;
  }
  board->prom = prom;
  board->prom_bytes = bytes;
  return true;
}

bool ap_board_reset_vector(const ap_board_t *board, uint32_t *stack,
                           uint32_t *pc) {
  if (board->prom == NULL || board->prom_bytes < 8u) {
    return false;
  }
  /* `[030]` §8.1: reset takes the interrupt stack pointer from the first long
   * word of the exception table and the program counter from the second. The
   * table is at address 0 after reset, before any VBR is written. */
  const uint8_t *p = board->prom;
  *stack = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
  *pc = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
        ((uint32_t)p[6] << 8) | (uint32_t)p[7];
  return true;
}

/* Serial 1 is unit 0 and the keyboard is on channel A. */
#define KBD_UNIT 0u
#define KBD_CHANNEL 0u

static bool deliver_key(ap_board_t *board, uint8_t code) {
  /* At the port's own rate: see the header on why this is an assumption rather
   * than a measurement. */
  const uint8_t csr =
      ap_sio_clock_select(&board->sio, KBD_UNIT, KBD_CHANNEL);
  ap_sio_receive_at(&board->sio, KBD_UNIT, KBD_CHANNEL, code, csr);
  return true;
}

bool ap_board_key_press(ap_board_t *board, unsigned key) {
  uint8_t code = 0;
  if (!ap_kbd_press(&board->keyboard, key, &code)) {
    return false;
  }
  return deliver_key(board, code);
}

bool ap_board_key_release(ap_board_t *board, unsigned key) {
  uint8_t code = 0;
  if (!ap_kbd_release(&board->keyboard, key, &code)) {
    return false;
  }
  return deliver_key(board, code);
}
