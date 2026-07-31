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
  if (address >= AP_BOARD_RAM_BASE) {
    return AP_BOARD_REGION_RAM;
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
  case AP_BOARD_REGION_RAM: {
    uint32_t offset = address - AP_BOARD_RAM_BASE;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break; /* past the memory actually fitted */
    }
    return board->ram[offset];
  }
  case AP_BOARD_REGION_PROM:
    /* No boot PROM image is loaded. Reported as unmapped rather than answered
     * with zero, because a machine that answers the PROM with zeros looks like
     * a machine with a blank PROM rather than one without one. */
    break;
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
  case AP_BOARD_REGION_RAM: {
    uint32_t offset = address - AP_BOARD_RAM_BASE;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break;
    }
    board->ram[offset] = value;
    return;
  }
  case AP_BOARD_REGION_NODE_ID:
  case AP_BOARD_REGION_PROM:
    /* Both are read-only memories. A write is not an error the board reports --
     * the hardware simply does not store it -- but it is not "ok" either, and
     * counting it is how a driver writing to a PROM becomes visible. */
    break;
  case AP_BOARD_REGION_UNMAPPED:
    break;
  }
  *ok = false;
  board->unmapped_writes++;
}
