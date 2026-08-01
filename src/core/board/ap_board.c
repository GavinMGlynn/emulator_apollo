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
  if (in(address, AP_BOARDREG_CPU_STATUS_ADDR, 4u * AP_BOARDREG_RANGE)) {
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
    if (ap_graphics_decode(address, &colour, &offset)) {
      return AP_BOARD_REGION_GRAPHICS;
    }
  }
  if (address >= AP_BOARD_RAM_BASE) {
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
  board->ram = ram;
  board->ram_bytes = ram_bytes;
  return true;
}

uint8_t ap_board_read(ap_board_t *board, uint32_t address, bool *ok) {
  *ok = true;
  switch (ap_board_region(address)) {
  case AP_BOARD_REGION_CORE_REGISTER:
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
  case AP_BOARD_REGION_TRANSLATION_MAP:
    return (uint8_t)(ap_atmap_read(&board->translation_map, address) & 0xFFu);
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
  board->unmapped_reads++;
  return 0xFFu;
}

void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok) {
  *ok = true;
  switch (ap_board_region(address)) {
  case AP_BOARD_REGION_CORE_REGISTER:
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
  case AP_BOARD_REGION_TRANSLATION_MAP:
    ap_atmap_write(&board->translation_map, address, value);
    return;
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
    board->rom_writes++;
    *ok = true;
    return;
  case AP_BOARD_REGION_UNMAPPED:
    break;
  }
  *ok = false;
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
