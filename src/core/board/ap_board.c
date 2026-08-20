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
static const ap_board_placement_t SERIES_4000_PLACEMENT[] = {
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
    /* **One 2681, aliased across the whole kilobyte** -- not two.
     *
     * Table 2-6 gives the DS3000 a single row, `008400`-`0087FF`, named "SIO",
     * where Table 2-8 gives the Series 4000 two 256-byte rows for SIO 1 and
     * SIO 2. §1.5.1 says why: "In the DS3000, the serial I/O control component
     * drives **two** asynchronous serial lines, SIO0 and SIO1; in the DS4000,
     * **four** ... SIO0, SIO1, SIO2, and SIO3", and a 2681 has two channels.
     * So a DS3000 carries one part and a DS4000 carries two.
     *
     * The oracle agrees independently and more bluntly: `dn3000_map` has
     * `map(0x008400, 0x0087ff)` to **`m_sio`** alone, and the DN3000 machine
     * configuration does `config.device_remove(APOLLO_SIO2_TAG)`.
     *
     * This was `2u * AP_SIO_RANGE`, which put a **second DUART** at `008500`
     * on a machine that has one, and left `008600`-`0087FF` unmapped where the
     * table says SIO. Four placements rather than one because `canonical` is
     * how this table expresses aliasing -- each 256-byte block folds onto
     * `AP_SIO1_ADDR`, so every one of them reaches unit 0. */
    {0x008400u, AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    {0x008500u, AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    {0x008600u, AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    {0x008700u, AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
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

/* ## Named for the architecture group, not for a machine
 *
 * This was `DS4000_MAP`, and the name was the one thing about it that was
 * wrong: the map is `008778-03` Table 2-8's, and Table 2-8 is the **Series
 * 4000** address space -- shared by every model `019411-A00` §4.2.1.4 puts in
 * that group, "DS3500, DS4000, DS4500, DS5500". The selector below reaches it
 * for any model carrying an address translation map, which is exactly that set;
 * naming it after one member made it read as a machine's map that the wrong
 * machines were being given.
 *
 * The DS5500 has since been split out -- `019411-A00` Table 2-5 replaces the
 * handbook's page wholesale for that model -- which leaves this serving the
 * DS3500, the DS4000 and the DS4500. The DS4000 itself is still absent from
 * `src/core/model/`; see `docs/COMPLETION_PLAN.md`. */
static const ap_board_map_t SERIES_4000_MAP = {
    .name = "Series 4000",
    .placement = SERIES_4000_PLACEMENT,
    .placements = sizeof SERIES_4000_PLACEMENT / sizeof SERIES_4000_PLACEMENT[0],
    .ram_base = AP_BOARD_RAM_BASE,
    .ram_limit = AP_BOARD_RAM_LIMIT,
    .prom_size = AP_BOARD_PROM_SIZE,
    .has_translation_map = true,
    /* "The Series 4000 makes use of all virtual address bits." */
    .address_mask = 0xFFFFFFFFu,
};

/* `019411-A00` Table 2-5, "DS5500 256-MB Physical Address Space Allocation",
 * which the addendum gives as a wholesale replacement for the handbook's page
 * 2-7. The DS5500 ran on `SERIES_4000_MAP` until this table was read, and it is a
 * near copy -- which is why the differences are worth naming rather than
 * leaving to a diff:
 *
 *   - **`011400` memory present register.** Table 2-8 has no such row at all,
 *     so this is the one placement here that exists on no other model. See
 *     `ap_boardreg.h` §4.2.1.18.
 *   - **No task alias at `010300`.** Table 2-5 runs `010000`, `010100`,
 *     `010200` and then jumps to `010400` for the first SIO. The Series 4000
 *     block of four 256-byte registers is a block of three here, and a DS5500
 *     access to `010300` therefore bus-errors where a DS3500's does not.
 *     Consistent with what the firmware already said: task alias is at no
 *     absolute address in any of the five boot images.
 *   - **64 MB of main memory, not 48.** Table 2-5 gives four 16 MB banks at
 *     `1000000`-`4FFFFFF` where Table 2-8 gives three. The addendum's replaced
 *     Figure 1-5 says "Main Memory (4 to 64 MB)" independently, and
 *     §4.2.1.18's own table tops out at four 16 MB boards -- three sources for
 *     the same ceiling.
 *   - **`010200` is a cache *status* register**, read-only, where Table 2-8's
 *     row is the cache control register this core measured on a DN3500. Not
 *     yet modelled separately; see `ap_boardreg.h` and `PROJECT_STATUS.md`.
 *
 * One further difference is recorded and **deliberately not implemented**:
 * Table 2-5 gives the address translation map `017000`-`017FFF`, 4 KB, where
 * `[S3K]` §2.5 gives 2 KB and this core's `AP_ATMAP_LIMIT` follows it. Widening
 * it needs the region size to become a property of the map rather than a
 * constant, because `AP_ATMAP_ENTRIES` is what `ap_board_hash_translation_map`
 * walks and growing it would change every model's state hash. Placing 4 KB
 * without that would alias the upper half onto the lower -- the exact fault
 * `ap_atmap.h` records the diagnostic catching. PROVISIONAL, and a named item
 * in `COMPLETION_PLAN.md`. */
static const ap_board_placement_t DS5500_PLACEMENT[] = {
    {AP_BOARD_PROM_BASE, AP_BOARD_PROM_SIZE, AP_BOARD_REGION_PROM,
     AP_BOARD_PROM_BASE},
    /* Three, not the Series 4000's four: no task alias. */
    {AP_BOARDREG_CPU_STATUS_ADDR, 3u * AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_CPU_STATUS_ADDR},
    {AP_BOARDREG_LATCH_PAGE_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_LATCH_PAGE_ADDR},
    {AP_BOARDREG_MEMORY_PRESENT_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_MEMORY_PRESENT_ADDR},
    {AP_BOARDREG_MASTER_REQUEST_ADDR, AP_BOARDREG_RANGE,
     AP_BOARD_REGION_CORE_REGISTER, AP_BOARDREG_MASTER_REQUEST_ADDR},
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

static const ap_board_map_t DS5500_MAP = {
    .name = "DS5500",
    .placement = DS5500_PLACEMENT,
    .placements = sizeof DS5500_PLACEMENT / sizeof DS5500_PLACEMENT[0],
    .ram_base = AP_BOARD_RAM_BASE,
    /* Four 16 MB banks, `1000000`-`4FFFFFF`. */
    .ram_limit = AP_BOARD_RAM_LIMIT_DS5500,
    .prom_size = AP_BOARD_PROM_SIZE,
    .has_translation_map = true,
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

/* The Series 2500's map, **recovered from `2500_BOOT_16182_8` and nothing
 * else**: no manual on disk covers a Series 2500, `[S3K]` being Series
 * 3000/4000 and `[EH]` Rev 4 predating the machine. Detail and the census in
 * `PROJECT_STATUS.md`.
 *
 * Two facts shape it. The PROM region is **128 K**, proved by the image's own
 * self-checksum bounds and its `0001F040` reset PC, and the core device block
 * therefore starts at `020000` -- the byte after it -- which is the same rule
 * the other two families follow: a 32 K PROM and devices at `008000`, a 64 K
 * PROM and devices at `010000`, a 128 K PROM and devices at `020000`. Inside
 * the block every offset is the Series 4000's, confirmed against the six
 * addresses the firmware references absolutely, and the reset sequence's first
 * three writes to `$20800` are the posted-code pattern the DN3500 writes to
 * `$010800`.
 *
 * **The core register block at `0202xx` is deliberately NOT mapped**, and that
 * is a measurement rather than an omission. The firmware uses **thirty-two
 * registers at a four-byte stride** from `020200` to `02027C`, each referenced
 * independently, then byte-granular ones at `020280`/`020283`/`020284`/
 * `020287`, then `0202C0`, `0202CC`, `0202D0`, `0202D4`, `0202D8`. On a Series
 * 4000 that whole range is **one** register aliased across 256 bytes, so
 * thirty-two distinct four-byte registers cannot be it. Mapping the range there
 * anyway is what made `$1F078`'s poll spin: it writes `#$1` to `$202D4` and
 * wants the low nibble back, and the Series 4000's cache register keeps only
 * bit 7. So this follows the rule the DS3000 map states for its DMA page
 * register -- left out rather than guessed at, because a region that decodes to
 * nothing is a visible gap where one answering another machine's register is an
 * invisible error.
 *
 * The calendar, interrupt controller and node ID are absent for the weaker
 * reason that the firmware never references them: the `+0x10000` rule would
 * place them, but the rule is what the core block just falsified inside its own
 * range, so it is not carried past what has been seen.
 *
 * **What is deliberately absent is as important as what is here.** No disk, no
 * floppy, no tape: this machine's storage is SCSI through a chip its own error
 * messages call the `C90`, and its display a `VTGA` with a frame buffer -- both
 * named by the PROM's strings, neither an AT card, and neither modelled. The
 * Series 4000's OMTI placements carried across would be wrong addresses for
 * peripherals this machine does not have. Its ISA `140`/`148` are a **PC/AT bus
 * tester**, a diagnostic fixture the PROM knows how to drive rather than a card
 * a shipping machine carries, so they are not here either.
 *
 * So this map is what has been measured, and a boot will run out of it at the
 * first thing it wants that is not. That is the honest shape for a machine
 * whose peripherals are a different generation's. */
static const ap_board_placement_t DS2500_PLACEMENT[] = {
    {0x000000u, 0x020000u, AP_BOARD_REGION_PROM, AP_BOARD_PROM_BASE},
    /* **The core registers, and the firmware's second instruction needs them.**
     * `2500_BOOT_16182_8` resets to `0001F040` and does
     *
     *     move.b #$1F,$00020800    ; the posted code, already mapped here
     *     move.b #$FF,$000202D0
     *     move.b #$40,$000202CC
     *     move.b #$2, $00020800
     *
     * and the second of those was a bus error: nothing decoded `0202xx`. It is
     * the DN3500's `010200` cache-control page at this family's offset, and the
     * offset is not a guess -- the three device blocks already here are the
     * DN3500's at **exactly** `+$10000`, on three independent placements
     * (`AP_SIO1_ADDR` `010400` -> `020400`, `AP_TIMER_ADDR` `010800` ->
     * `020800`, `AP_DMA1_ADDR` `010C00` -> `020C00`). A fourth block at the
     * same displacement is the reading those three make, and the firmware
     * reaching into it is what says the block exists.
     *
     * `canonical` sends them to the DN3500 addresses the register module was
     * written against, which is what that field is for. */
    /* `020000` CPU status and `020100` CPU control: **these two are the
     * DN3500's**, at this family's displacement, and the firmware reads
     * `020000` immediately after the block above. Unlike `0202xx` they are
     * documented registers whose DN3500 originals this core already models, and
     * the displacement is the one three other blocks here already establish. */
    {0x020000u, 2u * AP_BOARDREG_RANGE, AP_BOARD_REGION_CORE_REGISTER,
     AP_BOARDREG_CPU_STATUS_ADDR},
    {0x020200u, AP_BOARDREG_RANGE, AP_BOARD_REGION_S2500_CONTROL, 0x020200u},
    /* Twenty-five references, and `[S3K]`-shaped usage: the serial ports. */
    {0x020400u, 2u * AP_SIO_RANGE, AP_BOARD_REGION_SIO, AP_SIO1_ADDR},
    /* The reset sequence's first three writes -- `#$1F`, `#$2`, `#$1C` -- are
     * the posted-code pattern the DN3500 writes to `$010800`. */
    {0x020800u, AP_TIMER_RANGE, AP_BOARD_REGION_TIMER, AP_TIMER_ADDR},
    {0x020C00u, 2u * AP_DMA_RANGE, AP_BOARD_REGION_DMA, AP_DMA1_ADDR},
};

/* `[CFG]` p. A-11 gives 4-16 MB, and the firmware sizes it: `OR.L
 * #$04000000,D1` at `$1F49A` puts the base in, and `ANDI.L #$04FFFFFF,D1` at
 * `$1F4CE` masks the walk -- a 16 MB region at `04000000`. */
static const ap_board_map_t DS2500_MAP = {
    .name = "DS2500",
    .placement = DS2500_PLACEMENT,
    .placements = sizeof DS2500_PLACEMENT / sizeof DS2500_PLACEMENT[0],
    .ram_base = 0x04000000u,
    .ram_limit = 0x04FFFFFFu,
    .prom_size = 0x020000u,
    .has_translation_map = false,
    .address_mask = 0xFFFFFFFFu,
};

const ap_board_map_t *ap_board_map_for(ap_model_id_t model) {
  const ap_model_t *entry = ap_model_by_id(model);
  /* The map follows the *translation map* feature, which is the one difference
   * the model table already records and which `019411-A00` §4.2.1.4 enumerates
   * by name: DS3500, DS4000, DS4500, DS5500 have it and a DS3000 does not. So
   * this asks the table rather than listing models again here. */
  /* The Series 2500 is its own family and cannot be told from the flags: it
   * has no translation map, like a DS3000, but its device block is at `020000`
   * rather than `008000` and its PROM is four times the size. So this one is
   * by model, and says why rather than looking like an oversight. */
  if (model == AP_MODEL_DN2500) {
    return &DS2500_MAP;
  }
  /* Also by model, and for the opposite reason to the 2500: the DS5500 has
   * every flag the DS3500 has, so the table cannot tell them apart. What
   * separates them is `019411-A00` Table 2-5 -- a register the DS3500 has not,
   * a register it has that the DS5500 has not, and a fourth memory bank. See
   * `DS5500_PLACEMENT`. */
  if (model == AP_MODEL_DN5500) {
    return &DS5500_MAP;
  }
  if (entry != NULL && !entry->has_address_translation_map) {
    return &DS3000_MAP;
  }
  return &SERIES_4000_MAP;
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

  /* Main memory first, ahead of the table it logically follows.
   *
   * This is a reordering, not a decision: on **both** maps every placement and
   * both graphics windows lie strictly below `ram_base`, so no address can
   * match this test and a test below it. `board_suite`'s
   * `test_no_device_placement_overlaps_the_memory_range` asserts exactly that,
   * for every map, which is what keeps the reordering honest when a placement
   * is added -- a device put inside the memory range fails the test rather than
   * silently reading as RAM.
   *
   * It is first because it is what the machine overwhelmingly asks about: a
   * boot's physical accesses are code and data, and the cache-inhibit callback
   * asks this question on *every* one of them. Answering it after a linear
   * scan of fifteen placements and two graphics decodes cost 6.3% of a boot. */
  if (address >= board->map->ram_base && address <= board->map->ram_limit) {
    /* The space allocated to memory, not the memory fitted. An address in here
     * with no SIMM behind it is still a main memory address -- the read path
     * bounds-checks against what is actually present and reports it unmapped --
     * which is the same distinction the AT bus windows make between an empty
     * slot and an address nothing decodes. */
    return AP_BOARD_REGION_RAM;
  }

  /* Then the model's own table, ahead of every window and decode below it.
   * Every base and size in it is the one the device's module carries, so a
   * placement corrected there cannot drift. */
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

  /* The ring controller's four windows, `RING.md` finding 38, before the AT
   * window they sit inside. Gated on the card being fitted rather than on the
   * address alone: an unfitted slot must keep reading `FF` from `ATBUS` below,
   * because that is what the option-ROM scan and every measurement so far have
   * seen, and because a card that answered when it was not there would be
   * indistinguishable from one that was. */
  if (board->ring.present &&
      ap_ring_ctl_decode(address, NULL, NULL, NULL)) {
    return AP_BOARD_REGION_RING;
  }

  /* The EtherLink Plus, on the same terms and before the AT windows for the
   * same reason: a window checked first reports a fitted card as an empty slot.
   * `ETHERNET.md` finding 2a places it at `058000` -- ISA `300H` through this
   * board's own `0x040000 + (ISA << 7)` -- and finding 10 confirmed that by
   * traffic, every oracle access landing on `058002` and `058006`. */
  if (board->ethernet_present &&
      ap_3c505_decode(AP_BOARD_ETHERNET_ADDR, address, NULL)) {
    return AP_BOARD_REGION_ETHERNET;
  }

  /* The Matrox graphics board's three blocks, on the same terms and ahead of
   * the AT *memory* window they sit inside. `GRAPHICS.md` finding 5 extracted
   * the bases mechanically from the board's own option ROM. */
  if (board->matrox_present && ap_matrox_decode(address, NULL, NULL)) {
    return AP_BOARD_REGION_MATROX;
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
   * here.
   *
   * **Except the Winchester, and `008778-03` §5.4.2 is what says so.** That
   * comment used to end "a card that asserts one is faster, and nothing on this
   * board is *known* to", which was true until the walk reached chapter 5: "Data
   * may be transferred to and from the host CPU in either a byte or word format.
   * **The Winchester disk uses the 16-bit (word) data transfer format; the
   * floppy disk uses the 8-bit (byte) data transfer format.**"
   *
   * So the fixed disk asserts `IO_CS16.L` and takes §2.4.2's **one** wait state
   * rather than four -- three bus clocks against six, half the time. The floppy
   * keeps the 8-bit cycle, which §5.4.2 states just as plainly and which is also
   * why it is the machine's only DMA device (§1.5.1). */
  if (in(address, AP_DISK_FIXED_ADDR, AP_DISK_FIXED_SIZE)) {
    return ap_atbus_access_time(timing, AP_ATBUS_CYCLE_IO_16, read);
  }
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

/* Table 2-8's last two registers, counted separately.
 *
 * Counted here rather than in the register module because it is the *machine*
 * that was watched, and a count is our record of watching rather than state the
 * board has -- which is why these stay out of the hash, like every other
 * counter.
 *
 * They were counted because they were declined, and they are still counted now
 * that they store: "which of these did this run touch" is worth answering
 * either way, and it is how the firmware's 29 write sites were confirmed to be
 * the master request register's and not the task alias's. The gate is the
 * address rather than `ap_boardreg_is_declined`, which now answers false for
 * both -- and did so silently until a test caught it. */
static void count_declined(ap_board_t *board, uint32_t address, bool read) {
  const bool in_master =
      address >= AP_BOARDREG_MASTER_REQUEST_ADDR &&
      address < AP_BOARDREG_MASTER_REQUEST_ADDR + AP_BOARDREG_RANGE;
  const bool in_alias =
      address >= AP_BOARDREG_TASK_ALIAS_ADDR &&
      address < AP_BOARDREG_TASK_ALIAS_ADDR + AP_BOARDREG_RANGE;
  if (!in_master && !in_alias) {
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

ap_time_t ap_board_interrupt_next_change(const ap_board_t *board) {
  ap_time_t next = AP_TIME_NEVER;
  const ap_time_t each[] = {
      ap_timer_interrupt_next_change(&board->timer),
      ap_sio_interrupt_next_change(&board->sio),
      ap_mc146818_interrupt_next_change(&board->calendar.rtc),
      ap_sc499_interrupt_next_change(&board->tape.controller),
      ap_omti_interrupt_next_change(&board->disk.controller),
  };
  for (unsigned i = 0; i < sizeof each / sizeof each[0]; i++) {
    if (each[i] < next) {
      next = each[i];
    }
  }
  return next;
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
   * nothing here is invented. The floppy's own line follows below, from the
   * FDC's result phase rather than this condition -- this comment used to say
   * it "is still absent", which was true when written and had been false since
   * the line twenty rows down was added.
   *
   * The boot PROM's driver polls, so a machine without this line still loaded
   * an operating system off the disk. Domain/OS's driver waits for the
   * interrupt, and printed `DISK TIMEOUT` when it never came. */
  ap_intr_set_request(&board->interrupts, AP_DISK_FIXED_IRQ,
                      ap_omti_disk_irq(&board->disk.controller));

  /* `IRQ6`, Table 2-3's floppy line, from the FDC's **result** phase -- its
   * completion, where §6.4's result bytes are waiting. A driver that waits
   * rather than polls needs this edge for the reason Domain/OS needed
   * `IRQ14`. */
  ap_intr_set_request(&board->interrupts, AP_DISK_FLOPPY_IRQ,
                      ap_omti_fdc_irq(&board->disk.controller));

  /* `IRQ10`, the 802.3 controller's, from `[DEV]` §1.10's two enables. Gated on
   * the card being *fitted*: an empty slot drives nothing, and a line held up by
   * an absent device would be this board asserting an interrupt no hardware
   * could produce. The card is opt-in, so this is the common case. */
  ap_intr_set_request(&board->interrupts, AP_BOARD_ETHERNET_IRQ,
                      board->ethernet_present &&
                          ap_3c505_irq(&board->ethernet));
  /* The ring, on master IRQ 2 -- `002398-04` p. 12-28, `RING.md` 107. The card
   * had no IRQ accessor and no wiring at all until the line was documented,
   * which is the "dangling shape the 3c505 had" that finding 82 named. */
  ap_intr_set_request(&board->interrupts, AP_BOARD_RING_IRQ,
                      ap_ring_ctl_irq(&board->ring));
  /* And the promise this sample stands on: nothing above can change before
   * this instant unless the bus is used. */
  board->interrupt_valid_until = ap_board_interrupt_next_change(board);
}

void ap_board_reset_devices(ap_board_t *board) {
  if (board == NULL) {
    return;
  }
  /* The parts a reset line on this board reaches. See
   * `AP_BOARDREG_CONTROL_RESET_DEVICES` for where the list comes from and for
   * why the SIO and the calendar are not in it -- the first because the page
   * says so, the second because its RAM is the node ID. */
  ap_intr_reset(&board->interrupts);
  ap_dma_reset(&board->dma);
  ap_dmapage_reset(&board->dma_page);
  /* `ap_timer_reset` re-establishes the three clock divisors as well as the
   * part, and it can fail only on a rate the time base does not divide -- which
   * is a configuration error caught at `ap_board_init`, not something a bus
   * write can introduce. */
  (void)ap_timer_reset(&board->timer);
}

bool ap_board_parity_interrupt(const ap_board_t *board) {
  /* A level, not an event, and so held rather than latched: `008778-03` §3.2
   * says "writing to the status register clears the interrupt status", which is
   * the only thing that lowers it. Deriving it from the two registers instead
   * of keeping a flag is what makes `clr.w $10000` and `019411-A00`'s Clear
   * Parity Error Flag both work without either being wired for. */
  return (board->registers.cpu_status & AP_BOARDREG_STATUS_PARITY_MASK) != 0u &&
         (board->registers.cpu_control &
          AP_BOARDREG_CONTROL_NMI_ENABLE) != 0u;
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

void ap_board_set_quirks(ap_board_t *board, ap_quirks_t quirks) {
  board->quirks = quirks;
  /* Pushed down rather than reached up for: a device asking the board for
   * configuration on every access would put a pointer chase on a hot path, and
   * the set does not change during a run. */
  board->graphics.quirks = quirks;
  for (unsigned unit = 0; unit < 2u; unit++) {
    board->sio.port[unit].quirks = quirks;
  }
}

void ap_board_attach_option_rom(ap_board_t *board, const uint8_t *image,
                                uint32_t bytes, uint32_t base) {
  if (board == NULL) {
    return;
  }
  board->option_rom = image;
  board->option_rom_bytes = (image != NULL) ? bytes : 0u;
  board->option_rom_base = base;
}

void ap_board_attach_ethernet(ap_board_t *board, bool fitted,
                              const uint8_t *address) {
  if (board == NULL) {
    return;
  }
  board->ethernet_present = fitted;
  ap_3c505_reset(&board->ethernet);
  ap_3c505_adapter_init(&board->ethernet_adapter, address);
}

void ap_board_attach_master(ap_board_t *board, unsigned unit, unsigned channel,
                            unsigned drq) {
  ap_master_init(&board->master, unit, channel, drq);
}

/* Both of them, together, every time a station is initialised -- `init` clears
 * the pointers, so lending has to follow it rather than precede it. The board
 * owns the storage; see `ap_board_t::ring_tx_bits` for why it lives there, and
 * for what was broken while nothing called either of these. */
static void board_lend_ring_buffers(ap_board_t *board) {
  ap_ring_station_attach_tx(&board->ring_station, board->ring_tx_bits,
                            sizeof board->ring_tx_bits);
  ap_ring_station_attach_rx(&board->ring_station, board->ring_rx_frame,
                            sizeof board->ring_rx_frame);
}

void ap_board_attach_ring(ap_board_t *board, bool fitted) {
  ap_ring_ctl_reset(&board->ring, fitted);
  /* The board's own node, so the ring's first window reads the same identity
   * the node ID PROM does (`RING.md` 93). */
  ap_ring_ctl_set_node_id(&board->ring, board->node_id.id);
}

/* One participant step: this node's bit time. The scheduler advances the
 * medium on its own clock, so a node drives, is driven, and hands whatever it
 * accepted to the controller -- and never touches the cable. */
static void board_ring_step(void *context, ap_time_t now) {
  (void)now;
  ap_board_t *board = (ap_board_t *)context;
  if (board->ring.medium == NULL) {
    return;
  }
  ap_ring_station_drive(&board->ring_station, board->ring.medium);
  ap_ring_station_receive(&board->ring_station, board->ring.medium);
  ap_ring_ctl_poll_ring(&board->ring);
}

int ap_board_join_ring_sched(ap_board_t *board, ap_ring_sched_t *sched) {
  if (board == NULL || sched == NULL) {
    return -1;
  }
  /* At the ring's own bit rate: a node's *station* is clocked by the cable,
   * not by its CPU. The processor's period belongs to a different participant
   * when a whole machine is scheduled; this is the ring card. */
  const int slot =
      ap_ring_sched_add(sched, AP_RING_DATA_HZ, board_ring_step, board);
  if (slot < 0) {
    return -1;
  }
  ap_ring_station_init(&board->ring_station, slot);
  board_lend_ring_buffers(board);
  ap_ring_ctl_attach_ring(&board->ring, &board->ring_station, &sched->medium);
  board->ring_scheduled = true;
  return slot;
}

void ap_board_join_ring(ap_board_t *board, ap_ring_medium_t *medium) {
  if (board == NULL) {
    return;
  }
  if (medium == NULL) {
    ap_ring_ctl_attach_ring(&board->ring, NULL, NULL);
    return;
  }
  const int slot = ap_ring_medium_attach(medium);
  if (slot < 0) {
    /* The segment is full. Refused rather than silently landing on slot -1,
     * which would index the medium's node array out of bounds the first time
     * `MISC_CMD` operated the bypass relay. */
    return;
  }
  ap_ring_station_init(&board->ring_station, slot);
  board_lend_ring_buffers(board);
  /* Attach *after* the reset in `ap_board_attach_ring`, never before: that
   * reset is also the controller's initialiser and clears the attachment
   * (`RING.md` 104d). */
  ap_ring_ctl_attach_ring(&board->ring, &board->ring_station, medium);
  board->ring_scheduled = false;
}

void ap_board_attach_matrox(ap_board_t *board, bool fitted) {
  board->matrox_present = fitted;
  ap_matrox_reset(&board->matrox);
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
  DMA_PERIPHERAL_ETHERNET,
} dma_peripheral_t;

static dma_peripheral_t dma_peripheral(const ap_board_t *board, unsigned unit,
                                       unsigned channel) {
  if (unit == AP_DMA_TAPE_UNIT && channel == AP_DMA_TAPE_CHANNEL) {
    return DMA_PERIPHERAL_TAPE;
  }
  if (unit == AP_DMA_FLOPPY_UNIT && channel == AP_DMA_FLOPPY_CHANNEL) {
    return DMA_PERIPHERAL_FLOPPY;
  }
  if (unit == AP_DMA_WINCHESTER_UNIT && channel == AP_DMA_WINCHESTER_CHANNEL) {
    return DMA_PERIPHERAL_WINCHESTER;
  }
  /* DRQ6, and only with a card in the slot. An unfitted channel keeps the
   * behaviour it has always had -- counted, and reading all ones -- which is
   * what `Table 2-4 assigns to something this core does not model` meant when
   * it named "either 802.3 controller" among them. */
  if (board->ethernet_present && unit == AP_DMA_ETHERNET_UNIT &&
      channel == AP_DMA_ETHERNET_CHANNEL) {
    return DMA_PERIPHERAL_ETHERNET;
  }
  return DMA_PERIPHERAL_NONE;
}

static uint8_t dma_device_read(void *context, unsigned channel) {
  ap_board_t *board = (ap_board_t *)context;
  switch (dma_peripheral(board, board->dma_transfer_unit, channel)) {
  case DMA_PERIPHERAL_TAPE:
    return ap_tape_dma_read(&board->tape);
  case DMA_PERIPHERAL_FLOPPY:
    return ap_disk_dma_read(&board->disk, true);
  case DMA_PERIPHERAL_WINCHESTER:
    return ap_disk_dma_read(&board->disk, false);
  case DMA_PERIPHERAL_ETHERNET:
    return ap_3c505_dma_read(&board->ethernet);
  case DMA_PERIPHERAL_NONE:
    break;
  }
  board->dma_unwired_transfers++;
  return 0xFFu;
}

static void dma_device_write(void *context, unsigned channel, uint8_t value) {
  ap_board_t *board = (ap_board_t *)context;
  switch (dma_peripheral(board, board->dma_transfer_unit, channel)) {
  case DMA_PERIPHERAL_TAPE:
    ap_tape_dma_write(&board->tape, value);
    return;
  case DMA_PERIPHERAL_FLOPPY:
    ap_disk_dma_write(&board->disk, true, value);
    return;
  case DMA_PERIPHERAL_WINCHESTER:
    ap_disk_dma_write(&board->disk, false, value);
    return;
  case DMA_PERIPHERAL_ETHERNET:
    ap_3c505_dma_write(&board->ethernet, value);
    return;
  case DMA_PERIPHERAL_NONE:
    break;
  }
  board->dma_unwired_transfers++;
}

void ap_board_bus_ticks(ap_board_t *board, uint64_t n) {
  if (n == 0u) {
    return;
  }
  if (!board->dma_possible && ap_arbiter_idle(&board->arbiter)) {
    board->bus_ticks += (unsigned)n;
    /* Once, not none: the tick still lowers the processor's request line, and
     * doing it once is the whole of what doing it `n` times would do. */
    ap_arbiter_tick(&board->arbiter);
    /* The master contends on the same clock the arbiter resolves. */
    ap_master_tick(&board->master, &board->dma.controller[0],
                   &board->arbiter);
    return;
  }
  for (uint64_t i = 0; i < n; i++) {
    ap_board_bus_tick(board);
  }
}

void ap_board_bus_tick(ap_board_t *board) {
  board->bus_ticks++;

  /* Nothing can be asking, so there is nothing to ask. See `dma_possible` in
   * the header: the three request sources are all software-started, and the
   * flag is re-armed by any access to their regions.
   *
   * The arbiter is still ticked, because it has its own idle guard and because
   * skipping it would be a second, unrelated claim. With nothing requesting,
   * the master cannot be the DMA line, so the early return below would have
   * been taken anyway -- this only avoids reaching it through four device
   * queries and two priority encodes. */
  if (!board->dma_possible) {
    ap_arbiter_tick(&board->arbiter);
    /* The master contends on the same clock the arbiter resolves. */
    ap_master_tick(&board->master, &board->dma.controller[0],
                   &board->arbiter);
    return;
  }

  /* Past the `dma_possible` guard, so a cycle here can reach a device and move
   * its interrupt line. The third and last site the bound is discarded at. */
  board->interrupt_valid_until = 0u;

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
  /* The tape asks while a read is in progress and there are bytes left, Table
   * 2-4's DRQ1. */
  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_TAPE_UNIT],
                           AP_DMA_TAPE_CHANNEL,
                           ap_tape_dma_request(&board->tape));

  /* And the Winchester on DRQ7, from the controller's own `DREQ`.
   * `board/ap_disk.h` said this had no line while only the register sets were
   * modelled -- "nothing in this controller knows a transfer is in progress" --
   * and that it gains one when the command sets do. They do, and it has.
   *
   * The floppy's DRQ2 is still absent, with the same boundary as its IRQ6: the
   * FDC's execution phase is a different condition from this one and lands with
   * the floppy's own item. */
  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_WINCHESTER_UNIT],
                           AP_DMA_WINCHESTER_CHANNEL,
                           ap_omti_disk_dma_request(&board->disk.controller));

  /* `DRQ2`, Table 2-4's floppy line. A *different* condition from the fixed
   * disk's -- the FDC's execution phase, a byte in flight, rather than a
   * command completing -- and gated on the Digital Output Register's
   * interrupt/DMA enable as the fixed disk's is gated on MASK. */
  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_FLOPPY_UNIT],
                           AP_DMA_FLOPPY_CHANNEL,
                           ap_omti_fdc_dma_request(&board->disk.controller));

  /* `DRQ6`, the 802.3 controller's, from `[DEV]` §1.9.4's three deactivating
   * conditions. This is the one request line on the board that is sampled *and*
   * advanced by the sampling: demand mode relinquishes the channel for one host
   * cycle every nine transfers, and this tick is that cycle. */
  ap_i8237_set_request_pin(
      &board->dma.controller[AP_DMA_ETHERNET_UNIT], AP_DMA_ETHERNET_CHANNEL,
      board->ethernet_present && ap_3c505_dma_request(&board->ethernet));

  ap_i8237_set_request_pin(&board->dma.controller[AP_DMA_CASCADE_UNIT],
                           AP_DMA_CASCADE_CHANNEL,
                           ap_i8237_service_pending(&board->dma.controller[0]) >=
                               0);

  const int selected =
      ap_i8237_service_pending(&board->dma.controller[AP_DMA_CASCADE_UNIT]);
  ap_arbiter_request(&board->arbiter, DMA_ARBITER_LINE, selected >= 0);

  /* Re-derived from what the poll just found, and left set while *any* request
   * line is still high even if masked -- a masked line still has to be cleared
   * when its device stops asking, and skipping that would leave `dreq` stale.
   * `dreq` is hashed state; the flag is not. */
  bool asking = selected >= 0;
  for (unsigned i = 0; i < 2u && !asking; i++) {
    asking = (board->dma.controller[i].dreq != 0u) ||
             (board->dma.controller[i].request != 0u);
  }
  board->dma_possible = asking;
  if (selected >= 0) {
    board->dma_bus_requests++;
  }

  /* The arbitration resolves first, and the master then uses the bus it has
   * just been given. The other order costs a clock at every handover and, worse,
   * makes the *first* clock of mastership do nothing -- which reads as a real
   * arbitration cost and is entirely this function's ordering. */
  ap_arbiter_tick(&board->arbiter);
  /* The master contends on the same clock the arbiter resolves. */
  ap_master_tick(&board->master, &board->dma.controller[0], &board->arbiter);

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
  const ap_i8237_cycle_t cycle =
      ap_i8237_transfer(&board->dma.controller[unit], &bus);
  if (cycle.ran) {
    board->dma_transfers++;
    /* The `EOP` a peripheral sees. Only the controller knows the transfer is
     * over -- the card counts bytes through a FIFO and has no length -- so the
     * terminal count is carried to it here, where the cycle that produced it
     * is. `[HIS]` p. 3-4 makes this `HSR`'s `DONE`, and `TCEN` turns it into an
     * interrupt on the line wired above. */
    if (cycle.terminal_count &&
        dma_peripheral(board, unit, cycle.channel) == DMA_PERIPHERAL_ETHERNET) {
      ap_3c505_dma_terminal_count(&board->ethernet);
    }
  }
}

/* Defined below, beside the press and release entry points it shares a queue
 * with. Declared here because a *repeat* is delivered from the advance rather
 * than from a caller's keystroke -- it is the one key event the board originates
 * itself. */
static bool deliver_key(ap_board_t *board, uint8_t code);

void ap_board_advance_one(ap_board_t *board, uint32_t address, ap_time_t now) {
  switch (ap_board_region(board, address)) {
  case AP_BOARD_REGION_RAM:
  case AP_BOARD_REGION_PROM:
  case AP_BOARD_REGION_S2500_CONTROL:
    /* Nothing to observe: none of them keeps time. The Series 2500 block is
     * storage with no modelled behaviour at all (see its declaration). */
    return;
  case AP_BOARD_REGION_SIO:
    ap_sio_advance(&board->sio, now);
    return;
  case AP_BOARD_REGION_CALENDAR:
    ap_calendar_advance(&board->calendar, now);
    return;
  case AP_BOARD_REGION_DISK:
    ap_omti_advance(&board->disk.controller, now);
    return;
  case AP_BOARD_REGION_TAPE:
    ap_tape_advance(&board->tape, now);
    return;
  /* Every other region falls back to the whole walk: slower and never wrong,
   * which is the right way round. Listed rather than defaulted because
   * `-Wswitch-enum` then makes a *new* region a compile error instead of a
   * silent fallback -- the failure this would otherwise hide is a device that
   * quietly stops advancing. */
  case AP_BOARD_REGION_UNMAPPED:
  case AP_BOARD_REGION_CORE_REGISTER:
  case AP_BOARD_REGION_TIMER:
  case AP_BOARD_REGION_DMA:
  case AP_BOARD_REGION_INTERRUPT:
  case AP_BOARD_REGION_NODE_ID:
  case AP_BOARD_REGION_TRANSLATION_MAP:
  case AP_BOARD_REGION_DMA_PAGE:
  case AP_BOARD_REGION_GRAPHICS:
  case AP_BOARD_REGION_RING:
  case AP_BOARD_REGION_ETHERNET:
  case AP_BOARD_REGION_MATROX:
  case AP_BOARD_REGION_ATBUS:
    ap_board_advance(board, now);
    return;
  }
}

void ap_board_advance(ap_board_t *board, ap_time_t now) {
  /* Each to the same instant, and each carrying its own remainder. Order does
   * not matter and must not: two devices advanced to the same absolute time
   * cannot influence each other through the advance itself, which is what makes
   * this a tick rather than a schedule. */
  /* Skipped until a pulse is due, on a simpler argument than the serial part's:
   * the PTM keeps no `now` of its own, only `clocked_to` per timer, so there is
   * nothing a skipped call could leave stale.
   *
   * The guard is cheaper than what it guards even though both walk three
   * timers -- this one only adds and compares, where the advance also subtracts
   * and may divide, and it is a call besides. That was not obvious and was got
   * wrong once: a first measurement said "neutral" and this was reverted, on
   * runs taken minutes apart on a machine whose wall time drifts by a second.
   * Interleaved A/B, three pairs, has it faster in all three. */
  if (now >= ap_timer_next_pulse(&board->timer)) {
    ap_timer_advance(&board->timer, now);
  }
  ap_calendar_advance(&board->calendar, now);
  /* **The ring, when the card has a cable.** `ap_ring_ctl_poll_ring` moves an
   * accepted frame into the buffer and raises `ri`; the bit-level driving of
   * the medium is the *frontend's*, because the medium is shared and no board
   * may advance a segment another board is also on -- doing it here would
   * advance the cable once per node.
   *
   * Gated on the join, so a machine with no ring segment does exactly what it
   * did before this existed. That is what keeps the reference boot hash and
   * the firmware self-test unchanged across `RING.md` 104-108. */
  if (board->ring.medium != NULL) {
    /* **The medium's bit clock**, `AP_RING_BIT_CELL_TICKS` base units per bit
     * at `[MAC]` §3.2's 12 Mbit/s. Driven here so frames move in a *running*
     * machine rather than only in tests -- until this, `ap_ring_station_drive`
     * and `_receive` were called by nothing outside the suites.
     *
     * **Only the segment's lowest attached slot advances the cable**, and the
     * guard is the point rather than an optimisation: the medium is shared, so
     * a board that advanced it unconditionally would advance a two-node ring
     * twice per bit time. Every board still drives and reads *its own* station
     * each bit, which is what a node does; one of them also steps the cable.
     *
     * That is correct for a segment inside one process, which is what
     * `--ring` builds. It is **not** the multi-node answer: nodes of different
     * models do not share a cycle, and `ap_ring_sched` exists for exactly that
     * -- N participants, each with its own period, ties broken by slot. Wiring
     * a real machine into it is `RING.md` 110a, and this guard is what keeps
     * the single-process case honest until then. */
    while (!board->ring_scheduled &&
           board->ring_bit_clock + AP_RING_BIT_CELL_TICKS <= now) {
      board->ring_bit_clock += AP_RING_BIT_CELL_TICKS;
      ap_ring_station_drive(&board->ring_station, board->ring.medium);
      if (ap_ring_medium_first_slot(board->ring.medium) ==
          board->ring_station.slot) {
        ap_ring_medium_advance(board->ring.medium);
      }
      ap_ring_station_receive(&board->ring_station, board->ring.medium);
    }
    ap_ring_ctl_poll_ring(&board->ring);
  }
  /* §3.9's memory refresh, which is a serial part doing a job that has nothing
   * to do with serial lines. It is here rather than absent because the counter
   * now has a clock: `board/ap_sio.h` derives the rate. */
  /* **Not gated on the next pulse, and that is a measurement rather than an
   * omission.** Skipping this until a pulse was due is provably safe -- the
   * part carries no `now` of its own, so nothing a skipped call leaves behind
   * can be stale -- and it made no measurable difference: interleaved A/B over
   * three pairs split 2-1 the other way, with minima favouring the ungated
   * build. A call per instruction is not worth carrying for that.
   *
   * **And the arithmetic says why**, which is the part worth keeping. §3.9's
   * refresh runs this counter off X1 for the life of the machine, and X1 is
   * 3.6 MHz against a 25 MHz CPU: 6.944 CPU clocks per pulse against a mean
   * instruction of 4.278, so 0.616 pulses land per instruction and only 38.4%
   * of instructions have no pulse due. A gate that fires five times in eight
   * cannot pay for itself one device at a time -- it is worth having only as
   * part of a whole-board bound that skips *every* advance at once, which is
   * the exact-skip item's remaining work and carries that same 38.4% ceiling.
   *
   * The *interrupt* bound is a different matter and does pay, because
   * `ap_sio_interrupt_next_change` bounds at terminal count rather than at the
   * next pulse: `ap_board_sample_interrupts` fell from 8.0% to 3.5%. */
  ap_sio_advance(&board->sio, now);
  /* The tape's command handshake, which is the only part of the drive that
   * moves with time -- §1.13.2's edges, at the bounds the figures publish. */
  ap_tape_advance(&board->tape, now);
  /* The Winchester's access time. A command that moved the heads completes here
   * rather than in the register write that issued it, and the interrupt it
   * raises is the one Domain/OS requires not to be instantaneous. */
  ap_omti_advance(&board->disk.controller, now);

  /* ## The keyboard repeats a held key, and nothing used to make it
   *
   * `ap_kbd_advance` has carried the part to `now` and reported a due repeat
   * since it was written, and **nothing called it** -- so a key held down was a
   * key struck once, and `AP_KBD_REPEAT_PERIOD`, `AP_KBD_REPEAT_DELAY` and
   * `AP_KBD_REPEAT_KEYSTATE` were all modelled from the manual and unreachable.
   * The header even said "nothing in this core advances time yet", which stopped
   * being true a long time before this.
   *
   * The repeat is **not the code again** in the keystate set. `[kbd]`: "The
   * repeat function is handled by the keyboard by transmitting a `7F` ... when
   * any key (except CAPS LOCK) has been pressed for longer than the repeat rate
   * time." A repeat that resent the down code would be indistinguishable from
   * the key being struck a second time, which is the whole reason the part
   * sends a distinct byte. */
  unsigned repeat_key = 0u;
  if (ap_kbd_advance(&board->keyboard, now, &repeat_key)) {
    (void)deliver_key(board, board->keyboard.keystate_mode
                                 ? AP_KBD_REPEAT_KEYSTATE
                                 : (uint8_t)repeat_key);
  }

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
    /* Queued, not delivered. See `ap_board.h`: the wire has a length, and a
     * reply that arrives all at once overruns a three-deep FIFO. A queue that
     * is already full drops the excess rather than wrapping over bytes still
     * waiting -- which is what a keyboard talking faster than the line can
     * carry would really do. */
    const unsigned room =
        (unsigned)(sizeof board->kbd_reply.bytes) - board->kbd_reply.count;
    const unsigned take = n < room ? n : room;
    for (unsigned i = 0; i < take; i++) {
      const unsigned at = (board->kbd_reply.head + board->kbd_reply.count + i) %
                          (unsigned)(sizeof board->kbd_reply.bytes);
      board->kbd_reply.bytes[at] = reply[i];
    }
    if (take > 0u && board->kbd_reply.count == 0u) {
      /* **A reply cannot begin before a character time has passed**, and the
       * queue used to let the first byte of a burst go on the very next
       * advance -- so an answer started arriving microseconds after the command
       * that provoked it, and only the bytes *after* the first were paced.
       *
       * That is not a detail. The boot PROM sends `00` and then, within
       * microseconds, resets the receiver (`CRA = 25`, whose miscellaneous
       * command flushes the FIFO) before starting the next exchange. On real
       * hardware the keyboard's answer is still on the wire at that moment and
       * the flush discards nothing; with the first byte delivered immediately,
       * it lands *before* the flush and the alignment of everything after it
       * differs.
       *
       * So the clock starts when the burst is queued, not when the first byte
       * goes. */
      board->kbd_reply.next_at =
          now + ap_sio_character_time(&board->sio, KBD_UNIT, KBD_CHANNEL,
                                      AP_SIO_KEYBOARD_BAUD);
    }
    board->kbd_reply.count += take;
  }

  /* And the wire itself, one character at a time. The rate is the channel's
   * own: `ap_sio_character_time` builds it from the receiver's mode registers
   * and the keyboard's clock select, so a firmware that reprograms the line
   * changes how fast its keyboard answers, which is what a shared wire does.
   *
   * A rate the part cannot name gives zero, and then the byte goes over at
   * once -- the old behaviour, kept for the case where nothing has programmed
   * the channel yet, because a character time of zero is not a reason to hold
   * traffic for ever. */
  while (board->kbd_reply.count > 0u && now >= board->kbd_reply.next_at) {
    ap_sio_receive_framed(&board->sio, KBD_UNIT, KBD_CHANNEL,
                          board->kbd_reply.bytes[board->kbd_reply.head],
                          AP_SIO_KEYBOARD_CSR, AP_SIO_KEYBOARD_MR1);
    board->kbd_reply.head = (board->kbd_reply.head + 1u) %
                            (unsigned)(sizeof board->kbd_reply.bytes);
    board->kbd_reply.count--;
    const ap_time_t character = ap_sio_character_time(
        &board->sio, KBD_UNIT, KBD_CHANNEL, AP_SIO_KEYBOARD_BAUD);
    if (character == 0u) {
      board->kbd_reply.next_at = now;
      break;
    }
    board->kbd_reply.next_at = now + character;
  }

  /* The transmitters, emptied by the board rather than by whoever happens to be
   * watching. One character time apart, from the channel's own framing, and the
   * keyboard's channel excluded because `kbd_reply` already carries it. */
  for (unsigned unit = 0; unit < 2u; unit++) {
    for (unsigned channel = 0; channel < 2u; channel++) {
      if (unit == KBD_UNIT && channel == KBD_CHANNEL) {
        continue;
      }
      if (now < board->tx[unit][channel].next_at) {
        continue;
      }
      uint8_t shifted = 0u;
      if (!ap_sio_transmit(&board->sio, unit, channel, &shifted)) {
        continue;
      }
      const unsigned cap = (unsigned)(sizeof board->tx[unit][channel].bytes);
      if (board->tx[unit][channel].count == cap) {
        /* Full: the oldest is gone, as a byte shifted into nothing is. */
        board->tx[unit][channel].head =
            (board->tx[unit][channel].head + 1u) % cap;
        board->tx[unit][channel].count--;
      }
      const unsigned at = (board->tx[unit][channel].head +
                           board->tx[unit][channel].count) % cap;
      board->tx[unit][channel].bytes[at] = shifted;
      board->tx[unit][channel].count++;
      const ap_time_t character =
          ap_sio_character_time(&board->sio, unit, channel,
                                AP_SIO_KEYBOARD_BAUD);
      board->tx[unit][channel].next_at = character == 0u ? now : now + character;
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
  case AP_BOARD_REGION_S2500_CONTROL:
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
  case AP_BOARD_REGION_ETHERNET:
  case AP_BOARD_REGION_RING:
  case AP_BOARD_REGION_MATROX:
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
  case AP_BOARD_REGION_ETHERNET: return "EtherLink Plus";
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
  case AP_BOARD_REGION_RING: return "token ring controller";
  case AP_BOARD_REGION_MATROX: return "Matrox graphics";
  case AP_BOARD_REGION_S2500_CONTROL: return "Series 2500 control";
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
  /* Armed at reset. A zeroed board has nothing asking, so `false` would in fact
   * be correct -- but the flag's whole safety argument is that it errs towards
   * doing the work, and starting it from a `memset` rather than from an
   * explicit decision is how that argument stops being checkable. */
  board->dma_possible = true;
  board->map = ap_board_map_for(model);
  ap_boardreg_init(&board->registers);
  {
    /* Machine variance out of the one table, never a conditional here. */
    const ap_model_t *entry = ap_model_by_id(model);
    ap_boardreg_set_active_low_lanes(
        &board->registers,
        entry == NULL || entry->has_active_low_parity_lanes);
    /* `019411-A00` §4.2.1.14: on a DS5500 `010200` is a read-only *status*
     * register and not the cache control register Table 2-8 puts there. By
     * model, like the map, because no flag in the table distinguishes them --
     * and the register file is given the answer rather than deciding it. */
    ap_boardreg_set_ds5500_cache_status(&board->registers,
                                        model == AP_MODEL_DN5500);
    /* "a graphics device is in the HSI connector". A DSP5500 is this board
     * without a display, which is exactly what the bit reports. */
    ap_boardreg_set_hsi_graphics(&board->registers,
                                 entry != NULL &&
                                     entry->display != AP_DISPLAY_NONE);
  }
  {
    /* What `011400` reports, on the model that has it: which slots hold boards
     * and how big each one is.
     *
     * **From the strap table's layout, not from dividing the total by four.**
     * The two are the same for 16 and 32 MB and differ everywhere else -- 20 MB
     * is `8-4-4-4` and 12 MB is `4-4-4-0` -- and on this very model they differ
     * at a size that divides cleanly: the DN5500's own decode chain makes 16 MB
     * `8-8-0-0`. Dividing would have had the register report four 4 MB boards
     * on a machine whose firmware says it has two 8 MB ones, which is a
     * disagreement SELF_TEST is built to notice.
     *
     * Left at its "(No Board)" reset when the model and size are not a row, or
     * when a row names a board size §4.2.1.18 has no code for -- a Series
     * 3000's 2 MB. Both are honest silences rather than invented values, and
     * neither can arise on a machine that has this register. */
    unsigned slots[AP_SIO_RAM_BANKS];
    if (ap_sio_ram_bank_layout(model, ram_bytes, slots, AP_SIO_RAM_BANKS)) {
      (void)ap_boardreg_set_memory_boards(&board->registers, slots,
                                          AP_BOARDREG_MEM_PRESENT_SLOTS);
    }
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
  /* And no ring board either, for a different reason than the display's: the
   * display's blocks decode with no screen fitted because the board carries
   * them, whereas the ring controller is an expansion card and an empty slot
   * decodes nothing at all. `ap_board_attach_ring` fits one. */
  ap_ring_ctl_reset(&board->ring, false);
  ap_kbd_reset(&board->keyboard);
  /* The wire is empty, and explicitly so rather than by the `memset` above: a
   * reset that left a reply half-delivered would put a byte from the previous
   * machine into the next one's first exchange. */
  board->kbd_reply.head = 0u;
  board->kbd_reply.count = 0u;
  board->kbd_reply.next_at = 0u;
  memset(board->tx, 0, sizeof board->tx);
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

/* Remember an empty-slot address once, in the order first seen.
 *
 * A linear scan of at most sixteen entries, on a path that a polling driver
 * takes millions of times. That is deliberate: the alternative is a hash or a
 * sorted set, and neither is worth its complexity for sixteen slots -- but the
 * *reason* it is cheap enough is that the list fills and then every later read
 * is sixteen compares and a return, with no writes and no growth. Once the list
 * is full only the dropped counter moves. */
static void note_atbus_empty_address(ap_board_t *board, uint32_t address) {
  for (unsigned i = 0; i < board->atbus_empty_distinct; i++) {
    if (board->atbus_empty_addresses[i] == address) {
      return;
    }
  }
  if (board->atbus_empty_distinct >= AP_BOARD_ATBUS_EMPTY_ADDRESSES) {
    board->atbus_empty_addresses_dropped++;
    return;
  }
  board->atbus_empty_addresses[board->atbus_empty_distinct++] = address;
}

uint8_t ap_board_read(ap_board_t *board, uint32_t address, bool *ok) {
  *ok = true;
  address &= board->map->address_mask;
  const ap_board_region_t counted = ap_board_region(board, address);
  /* The interrupt bound promised only that *time* would not move a line, so a
   * bus access discards it -- but **only one that can reach a device**.
   *
   * Every memory access arrives here too, and a read of RAM or PROM cannot
   * change any of the eight lines `ap_board_sample_interrupts` polls: they come
   * from the timer, the two serial parts, the calendar, the tape, the two disk
   * halves and the ethernet, and none of those is memory. Discarding the bound
   * for a RAM read is what a first version did, and it is why the sample stayed
   * at 8% of a profile after being made skippable -- a boot touches memory
   * almost every instruction, so the promise never survived long enough to be
   * used. Everything that is not plainly memory still discards it, including
   * unmapped space, because reasoning about a bus error is not worth the
   * cycles it would save. */
  if (counted != AP_BOARD_REGION_RAM && counted != AP_BOARD_REGION_PROM) {
    board->interrupt_valid_until = 0u;
  }
  /* Any access to a device that can start a DMA transfer re-arms the bus
   * tick's poll. Conservative on purpose: a *read* of a status register cannot
   * start a transfer, and it is included anyway, because the cost of being
   * wrong here is a silent stale request line and the cost of being
   * over-inclusive is the work that was already being done. This switch is the
   * auditable set of sites the flag needs. */
  if (counted == AP_BOARD_REGION_DISK || counted == AP_BOARD_REGION_TAPE ||
      counted == AP_BOARD_REGION_DMA ||
      counted == AP_BOARD_REGION_ETHERNET) {
    board->dma_possible = true;
  }
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
  case AP_BOARD_REGION_ETHERNET: {
    uint32_t offset = 0;
    (void)ap_3c505_decode(AP_BOARD_ETHERNET_ADDR, address, &offset);
    return ap_3c505_read(&board->ethernet, offset);
  }
  case AP_BOARD_REGION_MATROX: {
    uint32_t block = 0;
    uint32_t offset = 0;
    (void)ap_matrox_decode(address, &block, &offset);
    return ap_matrox_read8(&board->matrox, block, offset);
  }
  case AP_BOARD_REGION_S2500_CONTROL:
    /* Reads back what was written, which is the firmware's one measured
     * requirement of this block: `1F060` writes `#$1` to `0202D4`, reads it,
     * masks `$0F` and requires `$1`, spinning for ever if it does not get it. */
    return board->s2500_control[address & 0xFFu];
  case AP_BOARD_REGION_RING: {
    /* **Unit 1 is a second slot, and it is empty.** Finding 38 left open
     * whether its windows were a second board or a second decode of the first,
     * and the unit was decoded and then discarded -- so one fitted card
     * answered at both. Domain/OS settles it: its driver search probes both
     * units and prints `Apollo Token Ring test passed.` then `... failed.`,
     * failing at `0005A400`, which is unit 1's `MISC_STAT`. An absent slot must
     * read `FF` -- finding 40's rule, and the same answer the disk diagnostic
     * gives as `Drive 1 (not found)`. `RING.md` 134. */
    unsigned unit = 0u;
    bool second = false;
    uint32_t offset = 0;
    (void)ap_ring_ctl_decode(address, &unit, &second, &offset);
    return ap_ring_ctl_read8(unit == 0u ? &board->ring : NULL, second, offset);
  }
  case AP_BOARD_REGION_ATBUS:
    /* A fitted option ROM answers before the pull-ups do, which is the whole
     * of what "a card is in the slot" means to the scan below. */
    if (board->option_rom != NULL && address >= board->option_rom_base &&
        address < board->option_rom_base + board->option_rom_bytes) {
      return board->option_rom[address - board->option_rom_base];
    }
    /* The window decodes and nothing drives the data lines, so the pull-ups
     * answer. `FF` rather than unmapped: the cycle terminates normally on the
     * real machine, and reporting a fault here would crash an expansion ROM
     * scan that is supposed to simply find nothing. */
    if (board->atbus_empty_reads == 0u) {
      board->first_atbus_empty_read = address;
    }
    if (board->atbus_empty_reads == 0u ||
        address < board->lowest_atbus_empty_read) {
      board->lowest_atbus_empty_read = address;
    }
    if (address > board->highest_atbus_empty_read) {
      board->highest_atbus_empty_read = address;
    }
    board->atbus_empty_reads++;
    board->last_atbus_empty_read = address;
    note_atbus_empty_address(board, address);
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
  /* The interrupt bound promised only that *time* would not move a line, so a
   * bus access discards it -- but **only one that can reach a device**.
   *
   * Every memory access arrives here too, and a read of RAM or PROM cannot
   * change any of the eight lines `ap_board_sample_interrupts` polls: they come
   * from the timer, the two serial parts, the calendar, the tape, the two disk
   * halves and the ethernet, and none of those is memory. Discarding the bound
   * for a RAM read is what a first version did, and it is why the sample stayed
   * at 8% of a profile after being made skippable -- a boot touches memory
   * almost every instruction, so the promise never survived long enough to be
   * used. Everything that is not plainly memory still discards it, including
   * unmapped space, because reasoning about a bus error is not worth the
   * cycles it would save. */
  if (counted != AP_BOARD_REGION_RAM && counted != AP_BOARD_REGION_PROM) {
    board->interrupt_valid_until = 0u;
  }
  /* Any access to a device that can start a DMA transfer re-arms the bus
   * tick's poll. Conservative on purpose: a *read* of a status register cannot
   * start a transfer, and it is included anyway, because the cost of being
   * wrong here is a silent stale request line and the cost of being
   * over-inclusive is the work that was already being done. This switch is the
   * auditable set of sites the flag needs. */
  if (counted == AP_BOARD_REGION_DISK || counted == AP_BOARD_REGION_TAPE ||
      counted == AP_BOARD_REGION_DMA ||
      counted == AP_BOARD_REGION_ETHERNET) {
    board->dma_possible = true;
  }
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
    /* `002398-04` p. 12-8, control register bit 1: `rsa`, "reset on-board
     * devices". The register itself cannot do this -- `ap_boardreg` knows about
     * registers and not about the parts around them -- so the board watches the
     * bit as it goes by. See `ap_board_reset_devices`. */
    if ((board->registers.cpu_control & AP_BOARDREG_CONTROL_RESET_DEVICES) !=
        0u) {
      ap_board_reset_devices(board);
    }
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
  case AP_BOARD_REGION_ETHERNET: {
    uint32_t offset = 0;
    (void)ap_3c505_decode(AP_BOARD_ETHERNET_ADDR, address, &offset);
    ap_3c505_write(&board->ethernet, offset, value);
    return;
  }
  case AP_BOARD_REGION_MATROX: {
    uint32_t block = 0;
    uint32_t offset = 0;
    (void)ap_matrox_decode(address, &block, &offset);
    ap_matrox_write8(&board->matrox, block, offset, value);
    return;
  }
  case AP_BOARD_REGION_S2500_CONTROL:
    board->s2500_control[address & 0xFFu] = value;
    return;
  case AP_BOARD_REGION_RING: {
    /* Unit 1 is an empty slot, and a write into one goes nowhere. */
    unsigned unit = 0u;
    bool second = false;
    uint32_t offset = 0;
    (void)ap_ring_ctl_decode(address, &unit, &second, &offset);
    ap_ring_ctl_write8(unit == 0u ? &board->ring : NULL, second, offset, value);
    return;
  }
  case AP_BOARD_REGION_ATBUS:
    if (board->atbus_empty_writes == 0u) {
      board->first_atbus_empty_write = address;
    }
    if (board->atbus_empty_writes == 0u ||
        address < board->lowest_atbus_empty_write) {
      board->lowest_atbus_empty_write = address;
    }
    if (address > board->highest_atbus_empty_write) {
      board->highest_atbus_empty_write = address;
    }
    board->atbus_empty_writes++;
    board->last_atbus_empty_write = address;
    /* Writes share the list. A driver that writes a command register and then
     * polls a status register beside it is the case this exists for, and
     * splitting the two lists would hide exactly that pairing. */
    note_atbus_empty_address(board, address);
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

/* Type one character, which is a **different set** from `key_press`'s.
 *
 * `ap_kbd_press` sends a matrix index -- the keystate set, where the host does
 * the translating and sees transitions. In the compatibility set the keyboard
 * sends the *character code* instead, and that is the set a machine sitting at
 * a prompt is reading. So typing is not pressing: it is putting Table 12-1's
 * code on the wire, and `ap_kbd_encode` is the table lookup that produces it.
 *
 * Shift is not a separate transmission here. The shifted code *is* the
 * character, so `encode`'s shift flag says how a person would produce it and
 * nothing needs sending for it -- which is why a caller that dutifully pressed
 * a shift key first would send a byte the keyboard never sends in this set.
 *
 * The keypad's codes are two bytes, `FE` then the character. Both go, high byte
 * first, because a model that dropped the prefix makes keypad `7`
 * indistinguishable from the main one. */
bool ap_board_mouse_move(ap_board_t *board, int dx, int dy, bool left,
                         bool middle, bool right) {
  if (board == NULL) {
    return false;
  }
  uint8_t packet[AP_KBD_MOUSE_PACKET];
  const unsigned bytes = ap_kbd_mouse_packet(&board->keyboard, dx, dy, left,
                                             middle, right, packet);
  if (bytes == 0u) {
    return false;
  }
  /* All four bytes or none: the escape frames the three that follow it, so a
   * packet cut short by a full queue would make the *next* bytes sent look like
   * movement. Checked before anything is queued rather than discovered
   * half-way. */
  const unsigned room =
      (unsigned)(sizeof board->kbd_reply.bytes) - board->kbd_reply.count;
  if (room < bytes) {
    return false;
  }
  for (unsigned i = 0; i < bytes; i++) {
    if (!deliver_key(board, packet[i])) {
      return false;
    }
  }
  return true;
}

bool ap_board_key_type(ap_board_t *board, char ascii) {
  uint16_t code = 0u;
  bool shifted = false;
  if (board == NULL || !ap_kbd_encode(ascii, &code, &shifted)) {
    return false;
  }
  if ((code & AP_KBD_PREFIX) == AP_KBD_PREFIX) {
    if (!deliver_key(board, (uint8_t)(AP_KBD_PREFIX >> 8))) {
      return false;
    }
  }
  return deliver_key(board, (uint8_t)(code & 0x00FFu));
}

bool ap_board_transmitted(ap_board_t *board, unsigned unit, unsigned channel,
                          uint8_t *byte) {
  if (board == NULL || byte == NULL || unit >= 2u || channel >= 2u ||
      board->tx[unit][channel].count == 0u) {
    return false;
  }
  const unsigned cap = (unsigned)(sizeof board->tx[unit][channel].bytes);
  *byte = board->tx[unit][channel].bytes[board->tx[unit][channel].head];
  board->tx[unit][channel].head = (board->tx[unit][channel].head + 1u) % cap;
  board->tx[unit][channel].count--;
  return true;
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
