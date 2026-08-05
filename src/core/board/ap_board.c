#include "board/ap_board.h"

#include <string.h>

/* The keyboard's port: serial 1 channel A, confirmed from both the oracle's
 * machine configuration and the boot PROM's own poll loop. */
#define KBD_UNIT 0u
#define KBD_CHANNEL 0u

static bool in(uint32_t a, uint32_t base, uint32_t size) {
  return a >= base && a < base + size;
}

/* `008778-03` Table 2-8, the 64 MB space the DS3500 and DS4000 lay out. Each
 * `canonical` equals its `base`, because this is the map every device module in
 * `board/` was written against. */
static const ap_board_placement_t DS4000_PLACEMENT[] = {
    {AP_BOARD_PROM_BASE, AP_BOARD_PROM_SIZE, AP_BOARD_REGION_PROM,
     AP_BOARD_PROM_BASE},
    {AP_BOARDREG_CPU_STATUS_ADDR, 4u * AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_CPU_STATUS_ADDR},
    {AP_BOARDREG_LATCH_PAGE_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_LATCH_PAGE_ADDR},
    {AP_BOARDREG_MASTER_REQUEST_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_MASTER_REQUEST_ADDR},
    /* Table 2-8 gives this range a row, and `019411-A00` gives each address in
     * it a function. Absent from `DS3000_PLACEMENT` below because Table 2-6
     * has no such row -- the selective clears are Series 4000 like the map and
     * the master request register. */
    {AP_BOARDREG_SELECTIVE_CLEAR_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_SELECTIVE_CLEAR_ADDR},
    {AP_SIO1_ADDR, 2u * AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    {AP_TIMER_ADDR, AP_TIMER_RANGE, AP_BOARD_REGION_TIMER, AP_TIMER_ADDR},
    {AP_CALENDAR_ADDR, AP_CALENDAR_RANGE, AP_BOARD_REGION_CALENDAR,
     AP_CALENDAR_ADDR},
    {AP_DMA1_ADDR, 2u * AP_DMA_RANGE, AP_BOARD_REGION_DMA, AP_DMA1_ADDR},
    {AP_INTR_MASTER_ADDR, 2u * AP_INTR_RANGE, AP_BOARD_REGION_INTERRUPT,
     AP_INTR_MASTER_ADDR},
    {AP_NODEID_ADDR, AP_NODEID_RANGE, AP_BOARD_REGION_NODE_ID, AP_NODEID_ADDR},
    {AP_ATMAP_BASE, AP_ATMAP_LIMIT - AP_ATMAP_BASE + 1u,
     AP_BOARD_REGION_TRANSLATION_MAP, AP_ATMAP_BASE},
    {AP_DISK_FIXED_ADDR, AP_DISK_FIXED_SIZE, AP_BOARD_REGION_DISK,
     AP_DISK_FIXED_ADDR},
    {AP_DISK_FLOPPY_ADDR, AP_DISK_FLOPPY_SIZE, AP_BOARD_REGION_DISK,
     AP_DISK_FLOPPY_ADDR},
    {AP_TAPE_ADDR, AP_TAPE_RANGE, AP_BOARD_REGION_TAPE, AP_TAPE_ADDR},
};

/* Table 2-6, the DS3000's 16 MB space. The differences from the above are not
 * a shift: the device block moves from `010000` to `008000`, and *within* it the
 * DMA, interrupt and node-ID placements move again -- `010C00` to `009000`,
 * `011000` to `009400`, `011200` to `009600` -- so no single offset describes
 * it and a table is the only honest form.
 *
 * Absent here and present above: the address translation map, the task alias
 * and the master request register. All three are Series 4000 architecture, and
 * `FINDINGS.md` C110 confirmed the last of them from the firmware -- the master
 * request register is referenced by no Series 3000 boot PROM.
 *
 * Present in Table 2-6 and *not* modelled: the **DMA page register** at
 * `009200`, which is what a machine without a translation map uses to extend a
 * DMA address. It is left out rather than guessed at: no manual here gives its
 * bits, and a region that decodes to nothing is a visible gap where a region
 * answering zero would be an invisible one. */
static const ap_board_placement_t DS3000_PLACEMENT[] = {
    {0x000000u, 0x008000u, AP_BOARD_REGION_PROM, AP_BOARD_PROM_BASE},
    /* Status and control only: Table 2-6 has `008200-0083FF` NOT USED where the
     * DS4000 has its cache control register. */
    {0x008000u, 2u * AP_BOARDREG_RANGE, AP_BOARD_REGION_CORE_REGISTER,
     AP_BOARDREG_CPU_STATUS_ADDR},
    {0x009300u, AP_BOARDREG_RANGE, AP_BOARD_REGION_CORE_REGISTER,
     AP_BOARDREG_LATCH_PAGE_ADDR},
    {0x008400u, 2u * AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    {0x008800u, AP_TIMER_RANGE, AP_BOARD_REGION_TIMER, AP_TIMER_ADDR},
    {0x008900u, AP_CALENDAR_RANGE, AP_BOARD_REGION_CALENDAR, AP_CALENDAR_ADDR},
    {0x009000u, 2u * AP_DMA_RANGE, AP_BOARD_REGION_DMA, AP_DMA1_ADDR},
    {AP_DMAPAGE_ADDR, AP_DMAPAGE_RANGE, AP_BOARD_REGION_DMA_PAGE,
     AP_DMAPAGE_ADDR},
    {0x009400u, 2u * AP_INTR_RANGE, AP_BOARD_REGION_INTERRUPT,
     AP_INTR_MASTER_ADDR},
    {0x009600u, AP_NODEID_RANGE, AP_BOARD_REGION_NODE_ID, AP_NODEID_ADDR},
    {AP_DISK_FIXED_ADDR, AP_DISK_FIXED_SIZE, AP_BOARD_REGION_DISK,
     AP_DISK_FIXED_ADDR},
    {AP_DISK_FLOPPY_ADDR, AP_DISK_FLOPPY_SIZE, AP_BOARD_REGION_DISK,
     AP_DISK_FLOPPY_ADDR},
    {AP_TAPE_ADDR, AP_TAPE_RANGE, AP_BOARD_REGION_TAPE, AP_TAPE_ADDR},
};

static const ap_board_map_t DS4000_MAP = {
    .name = "DS4000",
    .placement = DS4000_PLACEMENT,
    .placements = sizeof DS4000_PLACEMENT / sizeof DS4000_PLACEMENT[0],
    .ram_base = AP_BOARD_RAM_BASE,
    .ram_limit = AP_BOARD_RAM_LIMIT,
    .prom_size = AP_BOARD_PROM_SIZE,
    .has_translation_map = true,
    /* "The Series 4000 makes use of all virtual address bits." */
    .address_mask = 0xFFFFFFFFu,
};

/* "MAIN MEMORY (FIRST MB)" through "(EIGHTH MB)", `100000` to `8FFFFF`. */
static const ap_board_map_t DS3000_MAP = {
    .name = "DS3000",
    .placement = DS3000_PLACEMENT,
    .placements = sizeof DS3000_PLACEMENT / sizeof DS3000_PLACEMENT[0],
    .ram_base = 0x00100000u,
    .ram_limit = 0x008FFFFFu,
    .prom_size = 0x008000u,
    .has_translation_map = false,
    /* "the five high-order (27:31) bits are simply ignored" */
    .address_mask = 0x07FFFFFFu,
};

const ap_board_map_t *ap_board_map_for(ap_model_id_t model) {
  const ap_model_t *entry = ap_model_by_id(model);
  /* The map follows the *translation map* feature, which is the one difference
   * the model table already records and which `019411-A00` §4.2.1.4 enumerates
   * by name: DS3500, DS4000, DS4500, DS5500 have it and a DS3000 does not. So
   * this asks the table rather than listing models again here. */
  if (entry != NULL && !entry->has_address_translation_map) {
    return &DS3000_MAP;
  }
  return &DS4000_MAP;
}

/* Where an address falls on this board, and the address the device module that
 * owns it expects to see. */
static bool locate(const ap_board_t *board, uint32_t address,
                   ap_board_region_t *region, uint32_t *canonical) {
  const ap_board_map_t *map = board->map;
  for (unsigned i = 0; i < map->placements; i++) {
    const ap_board_placement_t *p = &map->placement[i];
    if (in(address, p->base, p->size)) {
      *region = p->region;
      *canonical = p->canonical + (address - p->base);
      return true;
    }
  }
  return false;
}

ap_board_region_t ap_board_region(const ap_board_t *board, uint32_t address) {
  address &= board->map->address_mask;
  /* The model's own table first. Every base and size in it is the one the
   * device's module carries, so a placement corrected there cannot drift. */
  ap_board_region_t region = AP_BOARD_REGION_UNMAPPED;
  uint32_t canonical = 0;
  if (locate(board, address, &region, &canonical)) {
    return region;
  }

  {
    bool colour = false;
    uint32_t offset = 0;
    /* Both graphics decodes, and both before the AT bus windows below: the
     * graphics memories sit inside the AT memory window, so a window checked
     * first would report the machine's own frame buffer as an empty expansion
     * slot. Table 2-6 and Table 2-8 place them identically, so this is not
     * model variance. */
    if (ap_graphics_decode(address, &colour, &offset) ||
        ap_graphics_decode_memory(address, &colour, &offset)) {
      return AP_BOARD_REGION_GRAPHICS;
    }
  }

  if (address >= board->map->ram_base && address <= board->map->ram_limit) {
    /* The space allocated to memory, not the memory fitted. An address in here
     * with no SIMM behind it is still a main memory address -- the read path
     * bounds-checks against what is actually present and reports it unmapped --
     * which is the same distinction the AT bus windows make between an empty
     * slot and an address nothing decodes. */
    return AP_BOARD_REGION_RAM;
  }

  /* Last, so every device *inside* a window keeps its own region. */
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
  /* IRQ13, which is serial 1's OP7 and nothing else -- a line the machine can
   * raise by hand so a diagnostic can check the controllers report it.
   * `008778-03` §2.5; see `board/ap_sio.h`. */
  ap_intr_set_request(&board->interrupts, AP_SIO_DIAGNOSTIC_IRQ,
                      ap_sio_diagnostic_interrupt(&board->sio));
  /* And the master's own request line, which this board reports in the cache
   * register's bit 4. Set after every device, because it is *their* sum. */
  ap_boardreg_set_interrupt_pending(&board->registers,
                                    ap_intr_pending(&board->interrupts));
  ap_intr_set_request(&board->interrupts, AP_CALENDAR_IRQ,
                      ap_calendar_irq(&board->calendar));
  ap_intr_set_request(&board->interrupts, AP_TAPE_IRQ,
                      ap_tape_irq(&board->tape));
  /* The fixed disk's `IRQ14`, which the controller now derives from `IREQ` and
   * the MASK register's enable bit -- both of which it already keeps, so
   * nothing here is invented. `AP_DISK_FLOPPY_IRQ` is still absent: the floppy
   * side's completion condition is the FDC's result phase rather than this
   * one, and it lands with the floppy's own item.
   *
   * The boot PROM's driver polls, so a machine without this line still loaded
   * an operating system off the disk. Domain/OS's driver waits for the
   * interrupt, and printed `DISK TIMEOUT` when it never came. */
  ap_intr_set_request(&board->interrupts, AP_DISK_FIXED_IRQ,
                      ap_omti_disk_irq(&board->disk.controller));
}

bool ap_board_parity_interrupt(const ap_board_t *board) {
  /* A level, not an event, and so held rather than latched: `008778-03` §3.2
   * says "writing to the status register clears the interrupt status", which is
   * the only thing that lowers it. Deriving it from the two registers instead
   * of keeping a flag is what makes `clr.w $10000` and `019411-A00`'s Clear
   * Parity Error Flag both work without either being wired for. */
  return (board->registers.cpu_status & AP_BOARDREG_STATUS_PARITY_MASK) != 0u &&
         (board->registers.cpu_control &
          AP_BOARDREG_CONTROL_INTERRUPT_ENABLE) != 0u;
}

unsigned ap_board_interrupt_level(const ap_board_t *board) {
  /* Parity first, because it is level 7 and nothing the 8259s can raise
   * outranks it. `008778-03` §3.2: "The parity error interrupt is a
   * non-maskable interrupt to the CPU. It generates a Level 7 interrupt". */
  if (ap_board_parity_interrupt(board)) {
    return AP_BOARD_PARITY_LEVEL;
  }
  /* Measured, not transcribed: `FINDINGS.md` C12 started the interval timer by
   * hand and swept the CPU's mask -- taken at 5, blocked at 6. Zero is "no
   * interrupt", which is what level zero means on this part. */
  return ap_intr_pending(&board->interrupts) ? AP_INTR_CPU_LEVEL : 0u;
}

bool ap_board_attach_parity(ap_board_t *board, uint8_t *bad, uint32_t bytes) {
  return ap_parity_attach(&board->parity, bad, bytes, board->ram_bytes);
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
  /* The second controller's channels are the AT's **16-bit** ones, and two
   * things follow, both of which this got wrong.
   *
   * Its address register counts **words**. The bus carries A1-A16 for a 16-bit
   * channel -- there is no A0 to drive -- so the byte address is the register
   * shifted left by one. `019411-A00` §4.2.1.4's fields are stated against that
   * bus address: index `<16:10>`, offset `<9:1>`. Read against the register
   * instead, every 16-bit transfer lands half a page low, which is exactly the
   * "one page short" the boot diagnostic showed.
   *
   * And the width selects how the map is indexed. This was hardcoded to 8-bit,
   * then changed, then reverted on the false premise that the diagnostic
   * programs controller 1 -- it does not: it writes `010D01` through `010D1B`,
   * the odd byte addresses of controller **2**. */
  const bool wide = board->dma_transfer_unit == AP_DMA_CASCADE_UNIT;
  const uint32_t bus_address =
      wide ? ((uint32_t)dma_address << 1) : (uint32_t)dma_address;
  return ap_atmap_translate(&board->translation_map, bus_address,
                            wide ? AP_ATMAP_TRANSFER_16BIT
                                 : AP_ATMAP_TRANSFER_8BIT);
}

static uint8_t dma_memory_read(void *context, uint16_t address) {
  ap_board_t *board = (ap_board_t *)context;
  const uint32_t physical = dma_physical(board, address);
  board->dma_last_read = physical;
  bool ok = false;
  return ap_board_read(board, physical, &ok);
}

static void dma_memory_write(void *context, uint16_t address, uint8_t value) {
  ap_board_t *board = (ap_board_t *)context;
  const uint32_t physical = dma_physical(board, address);
  board->dma_last_write = physical;
  bool ok = false;
  ap_board_write(board, physical, value, &ok);
}

/* Which device answers a DACK on this controller and channel, from `008778-03`
 * Table 2-4. `unit` is the controller the transfer is running on, which the
 * caller knows and the part does not.
 *
 * A channel Table 2-4 assigns to something this core does not model -- the PC
 * coprocessor, either 802.3 controller, or a "User Device" -- is not an error
 * and is not silently zero: it is counted, and reads what nothing driving this
 * bus reads. */
typedef enum {
  DMA_PERIPHERAL_NONE,
  DMA_PERIPHERAL_TAPE,
  DMA_PERIPHERAL_FLOPPY,
  DMA_PERIPHERAL_WINCHESTER,
} dma_peripheral_t;

static dma_peripheral_t dma_peripheral(unsigned unit, unsigned channel) {
  if (unit == AP_DMA_TAPE_UNIT && channel == AP_DMA_TAPE_CHANNEL) {
    return DMA_PERIPHERAL_TAPE;
  }
  if (unit == AP_DMA_FLOPPY_UNIT && channel == AP_DMA_FLOPPY_CHANNEL) {
    return DMA_PERIPHERAL_FLOPPY;
  }
  if (unit == AP_DMA_WINCHESTER_UNIT && channel == AP_DMA_WINCHESTER_CHANNEL) {
    return DMA_PERIPHERAL_WINCHESTER;
  }
  return DMA_PERIPHERAL_NONE;
}

static uint8_t dma_device_read(void *context, unsigned channel) {
  ap_board_t *board = (ap_board_t *)context;
  switch (dma_peripheral(board->dma_transfer_unit, channel)) {
  case DMA_PERIPHERAL_TAPE:
    return ap_tape_dma_read(&board->tape);
  case DMA_PERIPHERAL_FLOPPY:
    return ap_disk_dma_read(&board->disk, true);
  case DMA_PERIPHERAL_WINCHESTER:
    return ap_disk_dma_read(&board->disk, false);
  case DMA_PERIPHERAL_NONE:
    break;
  }
  board->dma_unwired_transfers++;
  return 0xFFu;
}

static void dma_device_write(void *context, unsigned channel, uint8_t value) {
  ap_board_t *board = (ap_board_t *)context;
  switch (dma_peripheral(board->dma_transfer_unit, channel)) {
  case DMA_PERIPHERAL_TAPE:
    ap_tape_dma_write(&board->tape, value);
    return;
  case DMA_PERIPHERAL_FLOPPY:
    ap_disk_dma_write(&board->disk, true, value);
    return;
  case DMA_PERIPHERAL_WINCHESTER:
    ap_disk_dma_write(&board->disk, false, value);
    return;
  case DMA_PERIPHERAL_NONE:
    break;
  }
  board->dma_unwired_transfers++;
}

void ap_board_bus_tick(ap_board_t *board) {
  board->bus_ticks++;
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
  /* The one device on this board that can derive a request of its own: the tape
   * asks while a read is in progress and there are bytes left, Table 2-4's
   * DRQ1. The disk's two channels have no line -- `board/ap_disk.h` says why --
   * and a driver starts those with the 8237's software request. */
  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_TAPE_UNIT],
                           AP_DMA_TAPE_CHANNEL,
                           ap_tape_dma_request(&board->tape));

  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_CASCADE_UNIT],
                           AP_DMA_CASCADE_CHANNEL,
                           ap_i8237_service_pending(&board->dma.controller[0]) >=
                               0);

  const int selected =
      ap_i8237_service_pending(&board->dma.controller[AP_DMA_CASCADE_UNIT]);
  ap_arbiter_request(&board->arbiter, DMA_ARBITER_LINE, selected >= 0);
  if (selected >= 0) {
    board->dma_bus_requests++;
  }

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
  board->dma_bus_held++;

  /* Whose transfer it is follows from which channel the second controller
   * selected. Its channel 0 in cascade mode is not a transfer at all -- the
   * part refuses one, and correctly -- it is the first controller's turn. */
  const unsigned unit =
      (selected == (int)AP_DMA_CASCADE_CHANNEL &&
       ap_i8237_mode_of(&board->dma.controller[AP_DMA_CASCADE_UNIT],
                        AP_DMA_CASCADE_CHANNEL) == AP_I8237_MODE_CASCADE)
          ? 0u
          : AP_DMA_CASCADE_UNIT;

  /* Which controller the cycle is running on, so the device callbacks can tell
   * controller 1's channel 3 from controller 2's. The part passes only a
   * channel, because a `DACK` is all a peripheral sees. */
  board->dma_transfer_unit = unit;
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
  /* §3.9's memory refresh, which is a serial part doing a job that has nothing
   * to do with serial lines. It is here rather than absent because the counter
   * now has a clock: `board/ap_sio.h` derives the rate. */
  ap_sio_advance(&board->sio, now);
  /* The tape's command handshake, which is the only part of the drive that
   * moves with time -- §1.13.2's edges, at the bounds the figures publish. */
  ap_tape_advance(&board->tape, now);

  /* **The keyboard is on the other end of serial 1 channel A**, and it answers.
   * Anything the firmware transmits there reaches it, and what it says back
   * goes into the same port's receiver -- which is the wire that makes a
   * command channel a channel rather than a write-only port.
   *
   * Done here rather than at the register write because the reply is a device's
   * and the device only exists while time is passing; and drained every advance
   * so a firmware writing two bytes before this core looks again loses
   * neither. */
  uint8_t out = 0u;
  while (ap_sio_transmit(&board->sio, KBD_UNIT, KBD_CHANNEL, &out)) {
    uint8_t reply[AP_KBD_REPLY_MAX];
    const unsigned n =
        ap_kbd_receive(&board->keyboard, out, reply, AP_KBD_REPLY_MAX);
    for (unsigned i = 0; i < n; i++) {
      ap_sio_receive_framed(&board->sio, KBD_UNIT, KBD_CHANNEL, reply[i],
                            AP_SIO_KEYBOARD_CSR, AP_SIO_KEYBOARD_MR1);
    }
  }

  /* The raster, which is a *function* of the instant rather than an
   * accumulation -- so it carries no remainder and does not care how often
   * this is called. */
  ap_graphics_advance(&board->graphics, now);
}

bool ap_board_cache_inhibited(const ap_board_t *board, uint32_t address) {
  (void)board;
  /* Memory is cacheable; everything else is a device. Written as the two
   * positives rather than a list of device ranges, so a region added later is
   * uncacheable until someone decides otherwise -- which is the direction that
   * fails safely. */
  switch (ap_board_region(board, address)) {
  case AP_BOARD_REGION_RAM:
  case AP_BOARD_REGION_PROM:
    return false;
  case AP_BOARD_REGION_UNMAPPED:
  case AP_BOARD_REGION_CORE_REGISTER:
  case AP_BOARD_REGION_SIO:
  case AP_BOARD_REGION_TIMER:
  case AP_BOARD_REGION_CALENDAR:
  case AP_BOARD_REGION_DMA:
  case AP_BOARD_REGION_INTERRUPT:
  case AP_BOARD_REGION_NODE_ID:
  case AP_BOARD_REGION_TRANSLATION_MAP:
  case AP_BOARD_REGION_DMA_PAGE:
  case AP_BOARD_REGION_DISK:
  case AP_BOARD_REGION_TAPE:
  case AP_BOARD_REGION_GRAPHICS:
  case AP_BOARD_REGION_ATBUS:
    break;
  }
  return true;
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
  case AP_BOARD_REGION_DMA_PAGE: return "DMA page register";
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
  return ap_board_init_model(board, ram, ram_bytes, start, node_id,
                             AP_MODEL_DN3500);
}

bool ap_board_init_model(ap_board_t *board, uint8_t *ram, uint32_t ram_bytes,
                         const ap_mc146818_time_t *start, uint32_t node_id,
                         ap_model_id_t model) {
  memset(board, 0, sizeof *board);
  board->map = ap_board_map_for(model);
  ap_boardreg_init(&board->registers);
  {
    /* Machine variance out of the one table, never a conditional here. */
    const ap_model_t *entry = ap_model_by_id(model);
    ap_boardreg_set_active_low_lanes(
        &board->registers,
        entry == NULL || entry->has_active_low_parity_lanes);
  }
  ap_parity_init(&board->parity);
  ap_atmap_init(&board->translation_map);
  ap_intr_reset(&board->interrupts);
  if (!ap_timer_reset(&board->timer)) {
    return false;
  }
  if (!ap_calendar_reset(&board->calendar, start)) {
    return false;
  }
  ap_dma_reset(&board->dma);
  ap_dmapage_reset(&board->dma_page);
  if (!ap_sio_reset(&board->sio)) {
    return false;
  }
  /* Serial 1's input port is strapped to the **RAM configuration byte**, which
   * the boot PROM reads to size memory before it does anything else. An input
   * port answering zero is a machine with no memory fitted, and the firmware
   * polls it rather than proceeding.
   *
   * A size the table does not cover leaves the strap alone rather than guessing
   * -- and `board->ram_config_known` says which happened, because a machine
   * that cannot tell the firmware how much memory it has is a fact a run should
   * report rather than a silent zero. */
  board->ram_config_known =
      ap_sio_ram_config_byte(model, ram_bytes, &board->ram_config);
  if (board->ram_config_known) {
    ap_sio_set_ram_config(&board->sio, board->ram_config);
  }
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

/* A read whose parity check failed. `008778-03` §3.2 and §3.3 between them give
 * every part of this: the status bit for the lane, the latched page, and a
 * level 7 autovectored interrupt if the control register enables it.
 *
 * The latch is a *page* number and not an address -- `019411-A00` §4.2.1.4 has
 * the same `<25:10>` field as the address translation map entry, which is the
 * other place this machine names a physical page. */
static void parity_error(ap_board_t *board, uint32_t address, uint32_t offset) {
  ap_boardreg_latch_status(&board->registers, ap_parity_lane_bit(offset));
  board->registers.latch_page_on_parity = (uint16_t)(address >> 10);
}

uint8_t ap_board_read(ap_board_t *board, uint32_t address, bool *ok) {
  *ok = true;
  address &= board->map->address_mask;
  const ap_board_region_t counted = ap_board_region(board, address);
  if ((unsigned)counted < AP_BOARD_REGIONS) {
    board->region_reads[counted]++;
  }
  /* Device modules are addressed in the DN3500's space, whatever this board's
   * is. A DS3000 puts its serial ports at `008400`; `ap_sio_read` knows only
   * `010400`, and the map is what reconciles them. */
  {
    ap_board_region_t placed = AP_BOARD_REGION_UNMAPPED;
    uint32_t canonical = address;
    if (locate(board, address, &placed, &canonical)) {
      address = canonical;
    }
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
  case AP_BOARD_REGION_DMA_PAGE:
    return ap_dmapage_read(&board->dma_page, address);
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
    uint32_t offset = address - board->map->ram_base;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break; /* past the memory actually fitted */
    }
    if (ap_parity_check(&board->parity, offset)) {
      /* The check fails, and the *data* still arrives -- the F280s sit beside
       * the array, not in front of it. Self-test 7 depends on that: it reads
       * the longword into `d0` and the failure reporter prints it. */
      parity_error(board, address, offset);
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
  /* An access nothing answered is the CPU timeout the status register reports,
   * and `019411-A00` gives it a clear location of its own at `016408` -- a
   * condition that can be cleared is one something sets. See
   * `board/ap_boardreg.h`. */
  ap_boardreg_latch_status(&board->registers, AP_BOARDREG_STATUS_BUS_ERROR);
  return 0xFFu;
}

void ap_board_write(ap_board_t *board, uint32_t address, uint8_t value,
                    bool *ok) {
  *ok = true;
  address &= board->map->address_mask;
  const ap_board_region_t counted = ap_board_region(board, address);
  if ((unsigned)counted < AP_BOARD_REGIONS) {
    board->region_writes[counted]++;
  }
  /* Canonicalised as the read path is, and for the same reason. */
  {
    ap_board_region_t placed = AP_BOARD_REGION_UNMAPPED;
    uint32_t canonical = address;
    if (locate(board, address, &placed, &canonical)) {
      address = canonical;
    }
  }
  switch (counted) {
  case AP_BOARD_REGION_CORE_REGISTER: {
    unsigned seen = 0;
    for (; seen < board->core_register_write_count; seen++) {
      if (board->core_register_writes[seen] == address) {
        break;
      }
    }
    if (seen == board->core_register_write_count &&
        board->core_register_write_count <
            sizeof board->core_register_writes /
                sizeof board->core_register_writes[0]) {
      board->core_register_writes[board->core_register_write_count++] = address;
    }
  }
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
  case AP_BOARD_REGION_DMA: {
    unsigned seen = 0;
    for (; seen < board->dma_register_write_count; seen++) {
      if (board->dma_register_writes[seen] == address) {
        break;
      }
    }
    if (seen == board->dma_register_write_count &&
        board->dma_register_write_count <
            sizeof board->dma_register_writes /
                sizeof board->dma_register_writes[0]) {
      board->dma_register_writes[board->dma_register_write_count++] = address;
    }
  }
    ap_dma_write(&board->dma, address, value);
    return;
  case AP_BOARD_REGION_INTERRUPT:
    ap_intr_write(&board->interrupts, address, value);
    return;
  case AP_BOARD_REGION_DMA_PAGE:
    ap_dmapage_write(&board->dma_page, address, value);
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
    uint32_t offset = address - board->map->ram_base;
    if (board->ram == NULL || offset >= board->ram_bytes) {
      break;
    }
    board->ram[offset] = value;
    /* Parity is generated on every write, so this runs on every write and not
     * only the diagnostic ones -- an ordinary write is how a forced-bad byte
     * gets its correct parity back. */
    ap_parity_write(&board->parity, offset,
                    ap_boardreg_forced_lanes(&board->registers));
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
  /* Both directions: the bus does not care which way an unanswered cycle was
   * going. */
  ap_boardreg_latch_status(&board->registers, AP_BOARDREG_STATUS_BUS_ERROR);
}

bool ap_board_load_prom(ap_board_t *board, const uint8_t *prom,
                        uint32_t bytes) {
  /* The model's own PROM region, which is 32 KB on a DS3000 against the
   * DS4000's 64 -- Table 2-6 gives `000000-007FFF` where Table 2-8 gives
   * `000000-00FFFF`. An image that does not fit is not this machine's PROM, and
   * truncating it would run whatever happened to be in the first half. */
  if (prom == NULL || bytes == 0u || bytes > board->map->prom_size) {
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

static bool deliver_key(ap_board_t *board, uint8_t code) {
  /* **The keyboard's own framing, and it is a measurement now.**
   *
   * This sent at *the port's own rate*, with a comment saying the rate was an
   * assumption rather than a measurement. It is measured:
   * `apollo_kbd_device::device_reset` sets the line up with the comment
   * "keyboard comms is at 8E1, 1200 baud" and then
   * `set_data_frame(1, 8, PARITY_EVEN, STOP_BITS_1)` at 1200 both ways.
   *
   * Sending at the port's rate was the more forgiving mistake: it made every
   * keypress arrive cleanly whatever the firmware had programmed, which is a
   * machine where the cable always agrees. A real keyboard has one fixed
   * framing and a driver that mis-programs the port sees a framing or parity
   * error -- which is the whole reason `ap_sio_receive_framed` exists.
   *
   * `66` is the clock select for 1200 baud in `[68681]`'s set one, and `MR1` of
   * `03` is eight bits with parity enabled and the type field zero, which is
   * even. */
  ap_sio_receive_framed(&board->sio, KBD_UNIT, KBD_CHANNEL, code,
                        AP_SIO_KEYBOARD_CSR, AP_SIO_KEYBOARD_MR1);
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

/* One word cycle into the display controller's image memory. `offset` is a byte
 * offset from the base of that memory and `count` the access width. */
static bool graphics_word_cycle(ap_board_t *board, uint32_t offset,
                                unsigned count, uint32_t value) {
  const uint32_t word = offset / 2u;
  uint16_t data = 0u;
  uint16_t mem_mask = 0xFFFFu;

  if (count == 1u) {
    /* A byte lands in the lane its address names, and the mask says which --
     * the controller sees the whole word either way. */
    data = (uint16_t)((offset & 1u) != 0u ? (value & 0xFFu)
                                          : ((value & 0xFFu) << 8));
    mem_mask = (offset & 1u) != 0u ? 0x00FFu : 0xFF00u;
  } else {
    data = (uint16_t)value;
  }

  const ap_graphics_cycle_t cycle =
      ap_graphics_memory_cycle(&board->graphics, word, data, mem_mask);
  board->graphics_cycles++;
  board->graphics_planes_written += cycle.planes_written;
  if (cycle.unknown_mode) {
    board->graphics_unknown_mode_cycles++;
    if (board->graphics_unknown_mode_cycles == 1u) {
      board->first_graphics_unknown_mode = offset;
    }
  }
  return true;
}

/* The four transfer sizes the part has. `[030]` Table 7-2, *Size Signal
 * Encoding*, read from the page image: `SIZ1 SIZ0` of `01` is Byte, `10` Word,
 * `11` **3 Bytes**, `00` Long Word.
 *
 * Three bytes is not an exotic case, it is what misalignment produces: a long
 * word at an address with `A1 A0 = 01` goes out as a 3-byte cycle and then a
 * byte, and the boot PROM's memory test writes long words at `+5`, `+A` and
 * `+F` on purpose, precisely because a 68030 can and a 68000 cannot. Refusing
 * the size made those a bus error and stopped the machine in `Memory Module 1
 * Test # 0` with `Unexpected CPU bus error referencing 0100A005`. */
static bool transfer_size(unsigned count) {
  return count == 1u || count == 2u || count == 3u || count == 4u;
}

/* Whether the display controller's own word cycles can serve this access. Its
 * port is sixteen bits, so a whole number of words at an even address is one
 * cycle each; a 3-byte transfer is neither, and goes the byte way rather than
 * being rounded up to two words -- which is what the word loop would have done,
 * writing a byte past the end of the operand. */
static bool whole_words(unsigned count) { return count == 2u || count == 4u; }

bool ap_board_write_access(ap_board_t *board, uint32_t address, unsigned count,
                           uint32_t value) {
  if (!transfer_size(count)) {
    return false;
  }

  /* The fixed disk's data port is sixteen bits and a word access to it is one
   * cycle: served as two byte writes the second would land in the *status*
   * register, which is a reset. See `board/ap_disk.h`. */
  if (whole_words(count) && ap_disk_is_data_port(&board->disk, address)) {
    board->region_writes[AP_BOARD_REGION_DISK] += count;
    for (unsigned i = 0; i < count; i += 2u) {
      ap_disk_write16(&board->disk, address,
                      (uint16_t)(value >> ((count - 2u - i) * 8u)));
    }
    return true;
  }

  bool colour = false;
  uint32_t offset = 0;
  const bool graphics_memory =
      ap_graphics_decode_memory(address, &colour, &offset) &&
      (colour ? ap_graphics_is_colour(board->graphics.screen)
              : ap_graphics_is_monochrome(board->graphics.screen));

  if (graphics_memory &&
      ((address % 2u == 0u && whole_words(count)) || count == 1u)) {
    /* Word cycles, which is what the controller sees. A long word is two of
     * them; an *odd* word access is not something this bus issues, and falls
     * through to the byte loop rather than being silently realigned. */
    board->region_writes[AP_BOARD_REGION_GRAPHICS] += count;
    bool all = true;
    for (unsigned i = 0; i < count; i += (count == 1u ? 1u : 2u)) {
      all = graphics_word_cycle(board, offset + i, count == 1u ? 1u : 2u,
                                count == 1u
                                    ? value
                                    : (value >> ((count - 2u - i) * 8u))) &&
            all;
    }
    return all;
  }

  bool all = true;
  for (unsigned i = 0; i < count; i++) {
    bool ok = false;
    ap_board_write(board, address + i,
                   (uint8_t)(value >> ((count - 1u - i) * 8u)), &ok);
    all = all && ok;
  }
  return all;
}

bool ap_board_peek_ram(const ap_board_t *board, uint32_t address,
                       unsigned count, uint32_t *out) {
  if (board == NULL || out == NULL || board->ram == NULL ||
      !transfer_size(count)) {
    return false;
  }
  if (address < board->map->ram_base || address > board->map->ram_limit) {
    return false;
  }
  const uint32_t offset = address - board->map->ram_base;
  if (offset >= board->ram_bytes || count > board->ram_bytes - offset) {
    return false; /* past the memory actually fitted */
  }
  /* Big endian, as every other read of this array is, and the parity latch is
   * deliberately not consulted: an observer must not manufacture the interrupt
   * the run was about to take, or clear one it had already earned. */
  uint32_t value = 0;
  for (unsigned i = 0; i < count; i++) {
    value = (value << 8) | board->ram[offset + i];
  }
  *out = value;
  return true;
}

bool ap_board_read_access(ap_board_t *board, uint32_t address, unsigned count,
                          uint32_t *out) {
  if (out == NULL || !transfer_size(count)) {
    return false;
  }

  if (whole_words(count) && ap_disk_is_data_port(&board->disk, address)) {
    board->region_reads[AP_BOARD_REGION_DISK] += count;
    uint32_t word = 0;
    for (unsigned i = 0; i < count; i += 2u) {
      word = (word << 16) | ap_disk_read16(&board->disk, address);
    }
    *out = word;
    return true;
  }

  bool colour = false;
  uint32_t offset = 0;
  const bool graphics_memory =
      ap_graphics_decode_memory(address, &colour, &offset) &&
      (colour ? ap_graphics_is_colour(board->graphics.screen)
              : ap_graphics_is_monochrome(board->graphics.screen));

  if (graphics_memory && address % 2u == 0u && whole_words(count)) {
    board->region_reads[AP_BOARD_REGION_GRAPHICS] += count;
    uint32_t value = 0;
    for (unsigned i = 0; i < count; i += 2u) {
      value = (value << 16) |
              ap_graphics_memory_read_cycle(&board->graphics,
                                            (offset + i) / 2u);
    }
    *out = value;
    return true;
  }

  bool all = true;
  uint32_t value = 0;
  for (unsigned i = 0; i < count; i++) {
    bool ok = false;
    const uint8_t byte = ap_board_read(board, address + i, &ok);
    all = all && ok;
    value = (value << 8) | byte;
  }
  *out = value;
  return all;
}
