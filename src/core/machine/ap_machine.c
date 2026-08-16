/* A constructed machine. See ap_machine.h for why it exists and what it is not. */

#include "machine/ap_machine.h"

#include "cpu/m68030/ap_m68030_exception.h"

#include "cpu/m68030/ap_m68030_state.h"
#include "board/ap_board.h"
#include "board/ap_board_state.h"

/* Big endian throughout, matching the operand layer, so a long word written by
 * the operator reads back through an instruction as the same number. */
static uint32_t read_bytes(const ap_machine_t *machine, uint32_t address,
                           unsigned count) {
  uint32_t value = 0;
  for (unsigned i = 0; i < count; i++) {
    value = (value << 8) | machine->ram[address + i];
  }
  return value;
}

/* The read watch, mirroring `machine_store`'s. Range rather than equality, for
 * the same reason: a byte read out of the middle of a long word is a read of
 * that long word as far as anyone reading it is concerned. Called after the
 * access so the *value* is real -- on the write side the interesting fact is
 * that the instruction reached, and here it is what the machine answered. */
static void note_read(ap_machine_t *machine, uint32_t physical, unsigned size,
                      uint32_t value) {
  if (machine->watch_read_address == 0u ||
      physical > machine->watch_read_address ||
      machine->watch_read_address >= physical + size) {
    return;
  }
  machine->watch_reads++;
  machine->watch_read_pc = machine->executing_address != 0u
                               ? machine->executing_address
                               : machine->cpu.regs.pc;
  machine->watch_read_value = value;
  machine->watch_read_size = size;
}

/* With a board attached, an access is a board access -- byte at a time, because
 * the map is a map of devices and a device answers its own registers. `ok` is
 * false if *any* byte went unanswered: a long word half in a device and half in
 * nothing is not a transfer this machine can make. */
static bool board_read(ap_machine_t *machine, uint32_t address, unsigned count,
                       uint32_t *out) {
  /* The board is told the width, for the reason `board_write` gives and one
   * more: a read of the display controller's image memory *latches* while
   * reading, so two byte reads would latch twice and leave the guard latch
   * holding a byte pair rather than a word. */
  return ap_board_read_access(machine->board, address, count, out);
}

static bool board_write(ap_machine_t *machine, uint32_t address, unsigned count,
                        uint32_t value) {
  /* The board is told the *width*, not handed a byte at a time. Almost every
   * region is eight bits wide and the board still loops, but the display
   * controller is sixteen and a CPU access to its image memory is a blit cycle
   * with a byte mask -- two byte writes there would run two half-masked blits
   * where the hardware runs one. The width was known here and thrown away at
   * the boundary. */
  return ap_board_write_access(machine->board, address, count, value);
}

static void write_bytes(ap_machine_t *machine, uint32_t address, unsigned count,
                        uint32_t value) {
  for (unsigned i = 0; i < count; i++) {
    machine->ram[address + i] = (uint8_t)(value >> ((count - 1u - i) * 8u));
  }
}

/* An access wholly inside the RAM. Checked as a range rather than as a start
 * address: a long word beginning one byte inside the top of memory is not an
 * access this machine can serve, and letting it through would read past the
 * buffer. */
/* Count a refused access and remember where. The first and the last are the two
 * worth keeping: the first says what a run tripped over on its way up, and the
 * last says what it was doing when it stopped -- which is the question a boot
 * that ends in a fault actually poses. */
static void fault(ap_machine_t *machine, uint32_t address) {
  if (machine->bus_errors == 0u) {
    machine->first_bus_error = address;
  }
  machine->last_bus_error = address;
  /* And who asked for it. An address on its own says what was unanswered and
   * not what was running, and those are different questions -- a sizing probe
   * that expects to fault and a program that has followed a wild pointer look
   * identical without the PC. */
  machine->last_bus_error_pc = machine->cpu.regs.pc;
  machine->bus_errors++;
  const uint32_t pc = machine->cpu.regs.pc;
  for (unsigned i = 0; i < machine->distinct_fault_count; i++) {
    if (machine->fault_sites[i].pc == pc) {
      machine->fault_sites[i].count++;
      machine->fault_sites[i].last_address = address;
      return;
    }
  }
  if (machine->distinct_fault_count <
      sizeof machine->fault_sites / sizeof machine->fault_sites[0]) {
    machine->fault_sites[machine->distinct_fault_count++] =
        (ap_fault_site_t){.pc = pc,
                          .first_address = address,
                          .last_address = address,
                          .count = 1u};
    return;
  }
  /* Past the cap. Counted rather than discarded silently: a list that stops
   * naming places is indistinguishable from a machine that stopped faulting in
   * new ones, and this run has already been read the wrong way once. */
  machine->fault_sites_dropped++;
}

/* The MMU's own refusals, kept apart from the board's. Same shape and the same
 * reasoning: keyed by the instruction, with the span of logical addresses it
 * reached over. */
static void machine_mmu_faulted(void *context, uint32_t logical,
                                uint8_t function_code, bool write,
                                ap_m68030_mmu_fault_t reason) {
  (void)function_code;
  ap_machine_t *machine = (ap_machine_t *)context;
  machine->mmu_faults++;
  if (machine->mmu_fault_stop_address != 0u &&
      logical == machine->mmu_fault_stop_address) {
    machine->mmu_fault_stopped = true;
  }

  const uint32_t pc = machine->cpu.regs.pc;
  for (unsigned i = 0; i < machine->mmu_fault_site_count; i++) {
    if (machine->mmu_fault_sites[i].pc == pc) {
      machine->mmu_fault_sites[i].count++;
      machine->mmu_fault_sites[i].last_address = logical;
      return;
    }
  }
  if (machine->mmu_fault_site_count <
      sizeof machine->mmu_fault_sites / sizeof machine->mmu_fault_sites[0]) {
    machine->mmu_fault_sites[machine->mmu_fault_site_count++] =
        (ap_mmu_fault_site_t){.pc = pc,
                              .first_address = logical,
                              .last_address = logical,
                              .count = 1u,
                              .reason = reason,
                              .write = write};
    return;
  }
  machine->mmu_fault_sites_dropped++;
}

static bool in_range(const ap_machine_t *machine, uint32_t address,
                     uint32_t count) {
  return address <= machine->ram_bytes && count <= machine->ram_bytes - address;
}

static void machine_fill(void *context, uint32_t line_address,
                         uint8_t function_code, ap_m68030_fill_answer_t *out) {
  (void)function_code;
  ap_machine_t *machine = (ap_machine_t *)context;

  if (machine->board != NULL) {
    uint32_t value = 0;
    if (!board_read(machine, line_address, 4u, &value)) {
      fault(machine, line_address);
      out->termination = AP_M68030_TERM_BERR;
      out->burst_acknowledge = false;
      return;
    }
    out->termination = AP_M68030_TERM_STERM;
    out->burst_acknowledge = false;
    out->data[0] = value;
    note_read(machine, line_address, 4u, value);
    return;
  }

  if (!in_range(machine, line_address, 4u)) {
    /* "Outside the RAM is a bus error, not a wrap and not a zero." */
    fault(machine, line_address);
    out->termination = AP_M68030_TERM_BERR;
    out->burst_acknowledge = false;
    return;
  }

  out->termination = AP_M68030_TERM_STERM;
  /* No burst acknowledge: this memory answers one long word at a time, which is
   * what a device without a 32-bit synchronous port does. A machine that
   * claimed CBACK would have the cache fill four entries from one answer. */
  out->burst_acknowledge = false;
  out->data[0] = read_bytes(machine, line_address, 4u);
  /* A cache line fill is a read of every address in it. Reported as the line,
   * width four, because that is the access the bus actually made -- narrowing
   * it to the watched byte would claim a cycle the machine did not run. */
  note_read(machine, line_address, 4u, out->data[0]);
}

/* How many whole CPU clocks the addressed device makes the processor wait.
 *
 * The board answers a *duration* and this converts it, which is the same
 * division of labour the machine's own clock has: how long a port takes is a
 * property of the port, and how many clocks that costs is a property of the
 * processor asking. A DN3000 at 12 MHz and a DN4500 at 33 MHz pay different
 * numbers of wait states for the identical AT card, and neither figure is
 * written down anywhere -- both fall out of one published nanosecond figure and
 * two clock rates.
 *
 * Beyond the minimum, not the whole cycle. `[030]` §11.6 assumes "two-clock bus
 * cycles and no wait states", so two clocks are already charged by the state
 * machine and a device that answers within them inserts none. */
static unsigned machine_wait_states(void *context, uint32_t physical,
                                    bool read) {
  const ap_machine_t *machine = (const ap_machine_t *)context;
  if (machine->board == NULL || machine->cpu_clock.period == 0u) {
    return 0u;
  }

  const ap_time_t needed = ap_board_access_time(machine->board, physical, read);
  if (needed == 0u) {
    return 0u; /* no published figure: the minimum */
  }

  /* Rounded *up*: a device that needs part of a clock still holds the bus for
   * all of it, and a cycle counted short would make a slow card look faster
   * than the manual says it is. */
  const uint64_t clocks =
      (needed + machine->cpu_clock.period - 1u) / machine->cpu_clock.period;
  return clocks > AP_M68030_MIN_BUS_CLOCKS
             ? (unsigned)(clocks - AP_M68030_MIN_BUS_CLOCKS)
             : 0u;
}

/* The interrupt acknowledge cycle, answered by the board's controllers.
 *
 * Vectored, never autovectored: the Apollo pair carries vector bases `A0` and
 * `A8`, so the sixteen levels occupy `A0`-`AF`. `[030]` §8.1.9 gives three
 * outcomes and this can produce one -- a machine with no board never gets here,
 * because a machine with no board has nothing to raise a level in the first
 * place. */
static ap_m68030_iack_t machine_acknowledge(void *context, unsigned level) {
  ap_machine_t *machine = (ap_machine_t *)context;
  ap_m68030_iack_t out = {0};
  if (machine->board != NULL && level == AP_BOARD_PARITY_LEVEL &&
      ap_board_parity_interrupt(machine->board)) {
    /* The one exception to "vectored, never autovectored", and the manual says
     * so outright. `008778-03` §3.2, on the parity error interrupt: "When the
     * vector is fetched, it comes from the Level 7 **autovector** location in
     * the CPU exception table (0 x 07c)". Nothing answers this acknowledge
     * cycle because the memory array is not a programmable interrupt
     * controller -- and `007C` is 31 x 4, which is `24 + 7`.
     *
     * The firmware agrees from the other side: self-test 7 installs its handler
     * at `$7c` off a VBR of `01000400` and expects to land in it. */
    out.autovector = true;
    return out;
  }
  if (machine->board == NULL) {
    /* No device answers, which §8.1.9 makes a spurious interrupt rather than a
     * reason not to take one. Unreachable while a level can only come from a
     * board, and written rather than asserted because the unreachable branch is
     * the one that gets reached later. */
    out.bus_error = true;
    return out;
  }
  out.vector = ap_board_interrupt_acknowledge(machine->board);
  return out;
}

/* `CIIN`, answered by the board: a device address is never cached.
 *
 * A machine on flat RAM has no devices and inhibits nothing, which is what
 * keeps every probe figure exactly as it was. */
static bool machine_cache_inhibited(void *context, uint32_t address) {
  const ap_machine_t *machine = (const ap_machine_t *)context;
  if (machine->board == NULL) {
    return false;
  }
  return ap_board_cache_inhibited(machine->board, address);
}

/* A read of exactly `size` bytes at exactly `address`, for a device.
 *
 * Straight to the board, byte at a time, which is the only width its devices
 * have -- and *not* through the long-word helper the fill path uses, because
 * the whole point is not to touch the bytes either side. */
static bool machine_read_sized(void *context, uint32_t address, unsigned size,
                               uint32_t *value) {
  ap_machine_t *machine = (ap_machine_t *)context;
  if (machine->board == NULL) {
    return false;
  }
  if (!board_read(machine, address, size, value)) {
    fault(machine, address);
    return false;
  }
  note_read(machine, address, size, *value);
  return true;
}

static bool machine_store(void *context, uint32_t physical, uint32_t value,
                          unsigned size) {
  ap_machine_t *machine = (ap_machine_t *)context;

  /* The watch, before the write is attempted rather than after it succeeds: an
   * instruction that stored to an address the board refused still says which
   * instruction was reaching for it, and that is what a watch is for. Range
   * rather than equality, because a byte written into the middle of a long word
   * is a write to that long word as far as anyone reading it is concerned. */
  if (machine->watch_write_address != 0u &&
      physical <= machine->watch_write_address &&
      machine->watch_write_address < physical + size) {
    machine->watch_writes++;
    machine->watch_write_pc = machine->executing_address != 0u
                                  ? machine->executing_address
                                  : machine->cpu.regs.pc;
    machine->watch_write_value = value;
    machine->watch_write_size = size;
  }

  if (machine->board != NULL) {
    if (!board_write(machine, physical, size, value)) {
      fault(machine, physical);
      return false;
    }
    return true;
  }

  if (!in_range(machine, physical, size)) {
    fault(machine, physical);
    return false;
  }
  write_bytes(machine, physical, size, value);
  return true;
}

/* A long word of a translation table, wherever this machine keeps its memory.
 *
 * The descriptor paths below used to index `machine->ram` by *physical address*
 * and bounds-check it against `ram_bytes`. That is right for a probe on flat
 * memory and wrong for every machine with a board: a DN3500's RAM begins at
 * `01000000`, so a descriptor at `0100A004` compared against a 16 MB extent is
 * out of range and every table search would bus-error before reading anything.
 *
 * It went unseen because nothing had enabled translation. Every boot in this
 * project reported `0 descriptor fetch(es)` until the disk handed over a
 * Domain/OS diagnostic that uses the MMU. */
static bool table_read(ap_machine_t *machine, uint32_t physical,
                       uint32_t *out) {
  if (machine->board != NULL) {
    if (!board_read(machine, physical, 4u, out)) {
      fault(machine, physical);
      return false;
    }
    return true;
  }
  if (!in_range(machine, physical, 4u)) {
    fault(machine, physical);
    return false;
  }
  *out = read_bytes(machine, physical, 4u);
  return true;
}

static bool table_write(ap_machine_t *machine, uint32_t physical,
                        uint32_t value) {
  if (machine->board != NULL) {
    if (!board_write(machine, physical, 4u, value)) {
      fault(machine, physical);
      return false;
    }
    return true;
  }
  if (!in_range(machine, physical, 4u)) {
    fault(machine, physical);
    return false;
  }
  write_bytes(machine, physical, 4u, value);
  return true;
}

/* A `PMOVE` retired. Recorded with the instruction that made it, since "which
 * tree, loaded by what" is the question and the register alone answers only the
 * first half.
 *
 * The PC is taken the way the watch counters take it -- the frontend's
 * `executing_address` when it is stepping, the processor's own PC otherwise --
 * so a stepped run names the instruction rather than the word after it. */
static void machine_mmu_register_written(void *context,
                                         ap_m68030_mmu_register_t which,
                                         uint32_t high, uint32_t low) {
  ap_machine_t *machine = (ap_machine_t *)context;
  machine->mmu_writes_total++;
  if (machine->mmu_write_count >= AP_MACHINE_MMU_WRITES) {
    return; /* the total above keeps counting, so the overflow is visible */
  }
  const unsigned i = machine->mmu_write_count++;
  machine->mmu_writes[i].pc = machine->executing_address != 0u
                                  ? machine->executing_address
                                  : machine->cpu.regs.pc;
  machine->mmu_writes[i].high = high;
  machine->mmu_writes[i].low = low;
  machine->mmu_writes[i].which = (uint8_t)which;
}

/* A `PMOVE` read the other way -- register out to memory.
 *
 * Counted rather than logged with its PC, because the question is not "which
 * value came back" but **whether the program looked at all**. A kernel that
 * inspects what the firmware left in `CRP` before configuring the MMU behaves
 * differently from one that assumes, and no dump taken afterwards can tell the
 * two apart. */
static void machine_mmu_register_read(void *context,
                                      ap_m68030_mmu_register_t which,
                                      uint32_t high, uint32_t low) {
  ap_machine_t *machine = (ap_machine_t *)context;
  (void)high;
  machine->mmu_reads_total++;
  if ((unsigned)which < 8u) {
    machine->mmu_reads_mask |= (uint8_t)(1u << (unsigned)which);
  }
  const uint32_t at = machine->executing_address != 0u
                          ? machine->executing_address
                          : machine->cpu.regs.pc;
  /* Keyed by (register, value, PC) and counted, not appended.
   *
   * Appending kept the *earliest* 32 reads, which answers a different question
   * from the one usually asked of it: a boot performs 30 million `MMUSR` reads
   * and the interesting ones are whichever a *storm* is making, always late and
   * always past the cap. Distinct combinations are few -- a handler that reads
   * the same register at the same PC and gets the same answer is one fact,
   * however many million times it does it -- so this keeps what varies and
   * counts the repetition. The fault sites are keyed this way for the same
   * reason and it was learned the same way. */
  for (unsigned i = 0; i < machine->mmu_read_count; i++) {
    if (machine->mmu_reads[i].which == (uint8_t)which &&
        machine->mmu_reads[i].value == low && machine->mmu_reads[i].pc == at) {
      machine->mmu_reads[i].count++;
      return;
    }
  }
  if (machine->mmu_read_count < AP_MACHINE_MMU_WRITES) {
    const unsigned i = machine->mmu_read_count++;
    machine->mmu_reads[i].pc = at;
    machine->mmu_reads[i].count = 1u;
    machine->mmu_reads[i].which = (uint8_t)which;
    /* The value, which this deliberately did not keep. The comment above still
     * holds -- *whether* a program looked is usually the question -- but not
     * always: Domain/OS's `FIM_$BUS_ERR` reads `MMUSR` and then branches on it,
     * and "which fault does this core report" cannot be answered by a count.
     * `MMUSR` is sixteen bits and arrives in `low`. */
    machine->mmu_reads[i].value = low;
  }
}

/* The table search's descriptor fetch. A machine whose MMU is off never calls
 * this; one whose tables a program has built does. */
static bool machine_table_fetch(void *context, uint32_t physical,
                                bool long_format,
                                ap_m68030_descriptor_t *out) {
  ap_machine_t *machine = (ap_machine_t *)context;
  const unsigned words = long_format ? 2u : 1u;
  /* Charged to whoever asked. `ap_machine_translate` is an observer and sets
   * `probing` around its walk; everything else here is the machine's own table
   * search. See `probe_fetches` in the header for what conflating the two
   * cost. */
  if (machine->probing) {
    machine->probe_fetches++;
  } else {
    machine->table_fetches++;
  }
  (void)words;

  uint32_t upper = 0;
  if (!table_read(machine, physical, &upper)) {
    return false; /* "Returns false for a bus error", which sets B in the ATC */
  }
  if (!long_format) {
    *out = ap_m68030_descriptor_unpack_short(upper, false);
    return true;
  }
  uint32_t lower = 0;
  if (!table_read(machine, physical + 4u, &lower)) {
    return false;
  }
  *out = ap_m68030_descriptor_unpack_long(upper, lower, false);
  return true;
}

/* The history-bit update, which is the write half of a read-modify-write. The
 * bits live in the descriptor's status field: U is bit 3 of a short descriptor
 * and of a long one's lower word, M is bit 4. */
static bool machine_table_update(void *context, uint32_t physical,
                                 bool set_used, bool set_modified) {
  ap_machine_t *machine = (ap_machine_t *)context;
  machine->table_updates++;

  uint32_t descriptor = 0;
  if (!table_read(machine, physical, &descriptor)) {
    return false;
  }
  if (set_used) {
    descriptor |= UINT32_C(1) << 3;
  }
  if (set_modified) {
    descriptor |= UINT32_C(1) << 4;
  }
  return table_write(machine, physical, descriptor);
}

void ap_machine_init(ap_machine_t *machine, uint8_t *ram, uint32_t ram_bytes) {
  ap_machine_init_model(machine, ram, ram_bytes, AP_MODEL_DN3500);
}

void ap_machine_init_model(ap_machine_t *machine, uint8_t *ram,
                           uint32_t ram_bytes, ap_model_id_t model) {
  /* Every field, not merely the ones set below. A machine is a value the caller
   * creates -- usually on the stack -- and one whose behaviour depended on what
   * was in that memory beforehand would be a machine that is not reproducible,
   * which is the one thing this must be. The cache and ATC resets below clear
   * valid bits rather than data, by design, so without this the leftovers would
   * be whatever the caller happened to have. */
  *machine = (ap_machine_t){0};

  /* **After** the blanking above, not before. Setting it first and then
   * zeroing the struct left `model` null while everything downstream read it,
   * so a machine built as a DN3000 had no module calls and reported `CALLM`
   * illegal -- which `FINDINGS.md` C85 spent two campaigns failing to see,
   * because the probe compared everything except the status that said so. */
  machine->model = ap_model_by_id(model);

  machine->ram = ram;
  machine->ram_bytes = ram_bytes;
  machine->bus_errors = 0;

  /* The processor's rate, from the table rather than from whoever built the
   * machine. `cpu_hz` has been in the model row since Phase 0 and nothing in
   * the core read it: every machine a probe built ran at a rate of zero, which
   * `ap_clock_duration` turns into no elapsed time at all, so a DN3000 at
   * 12 MHz and a DN4500 at 33 MHz produced the same answer for the same
   * program -- the same shape as the decoder the step never asked.
   *
   * The failure is unreachable and still not swallowed: `ap_clock_init` refuses
   * a frequency `AP_TIME_BASE_HZ` does not divide exactly, and `time_suite`
   * asserts that it divides every model's, so a model added with an
   * unrepresentable rate fails there rather than quietly producing a zero-rate
   * machine here. */
  if (machine->model != NULL) {
    (void)ap_clock_init(&machine->cpu_clock, machine->model->cpu_hz);
  }

  ap_m68030_cache_clear(&machine->instruction_cache);
  ap_m68030_cache_clear(&machine->data_cache);
  ap_m68030_atc_flush(&machine->atc);

  machine->cpu = (ap_m68030_cpu_t){0};
  /* The one place a model changes the CPU's behaviour, read from the table
   * rather than decided here. */
  machine->cpu.has_module_calls =
      machine->model != NULL &&
      ap_cpu_features(machine->model->cpu).has_module_calls;
  ap_m68882_reset(&machine->fpu);
  /* **Every model in the table has a coprocessor**, so attaching one is not the
   * approximation it was once recorded as. `ap_m68882.h` says "a DN3500 has a
   * 68882 and a DN3000 does not", and that is about the *part*, not about
   * having one: a DN3000 carries an MC68881. Reading it as "no coprocessor"
   * produced a Phase 3 item to gate something that never needed gating.
   *
   * What is genuinely not expressed is 68881 *versus* 68882, and this core
   * already records why that is nearly nothing: the 68882's concurrency "is
   * invisible to a program except through timing". So the same module serves
   * both, and the model is consulted to say so rather than to choose. */
  machine->cpu.fpu = &machine->fpu;

  /* Both contexts point at the CPU's own MMU registers, so a PMOVE takes effect
   * on translation rather than only on a register nobody reads. */
  machine->instruction_access = (ap_m68030_access_ctx_t){
      .cache = &machine->instruction_cache,
      .atc = &machine->atc,
      .tc = &machine->cpu.tc,
      .root = &machine->cpu.crp,
      .tt0 = &machine->cpu.tt0,
      .tt1 = &machine->cpu.tt1,
      .cache_enabled = true,
      .fill = machine_fill,
      .store = machine_store,
      .table_fetch = machine_table_fetch,
      .table_update = machine_table_update,
      .mmu_register_written = machine_mmu_register_written,
      .mmu_register_read = machine_mmu_register_read,
      .mmu_faulted = machine_mmu_faulted,
      /* Always supplied, and it answers zero until a board is attached: a probe
       * on flat RAM has no device with a published cycle time, so nothing it
       * measures moves. Wiring it here rather than in `ap_machine_set_board`
       * keeps one construction path -- a callback installed by a setter is a
       * callback some caller forgets. */
      .wait_states = machine_wait_states,
      .inhibits_cache = machine_cache_inhibited,
      .read_sized = machine_read_sized,
      .context = machine,
  };
  machine->data_access = machine->instruction_access;
  machine->data_access.cache = &machine->data_cache;

  /* Installed unconditionally, like the wait-state callback and for the same
   * reason: one construction path. It is only ever consulted when a level is
   * standing, and only a board can raise one. */
  machine->cpu.acknowledge = machine_acknowledge;
  machine->cpu.acknowledge_context = machine;

  machine->cpu.fetch.access = &machine->instruction_access;
  machine->cpu.fetch.function_code = AP_M68030_FC_SUPERVISOR_PROGRAM;
  machine->cpu.data = &machine->data_access;
  machine->cpu.data_function_code = AP_M68030_FC_SUPERVISOR_DATA;
}

void ap_machine_reset(ap_machine_t *machine, uint32_t pc, uint32_t stack) {
  /* `[030]` §8.1.1's steps 1-7, from the one implementation of them. This used
   * to be a second, shorter sequence written here -- supervisor and mask 7 and
   * nothing else -- which set four of the ten steps aside: the master bit, the
   * vector base register, the cache control register, and the translation
   * control and transparent translation enables.
   *
   * That omission is invisible exactly once. A cold start runs on a zeroed
   * `ap_machine_t`, so VBR, CACR and every enable bit are already what reset
   * would have made them, and the short sequence and the real one agree. Every
   * *later* reset is on a machine that has been running, and there they do not:
   * a warm reset would have kept the old VBR and the old translation tree and
   * fetched its first instruction through them. */
  ap_m68030_reset_state(&machine->cpu);
  machine->cpu.regs.isp = stack;
  machine->cpu.clocks = 0;
  machine->bus_errors = 0;
  /* The profile goes with the count it describes. Zeroing one and not the other
   * leaves a site list that outlives its own total -- sixty-four places named
   * by a machine reporting no bus errors at all. */
  machine->distinct_fault_count = 0;
  machine->fault_sites_dropped = 0;
  machine->mmu_faults = 0;
  machine->mmu_fault_site_count = 0;
  machine->mmu_fault_sites_dropped = 0;
  machine->mmu_fault_stopped = false;
  machine->exception_stopped = false;
  machine->exception_stop_seen = 0u;
  machine->exception_stop_pc = 0u;

  /* Step 6, "Invalidates all entries in the instruction and data caches".
   *
   * The ATC is deliberately *not* flushed with them: "The reset exception does
   * not flush the address translation cache (ATC)". It was flushed here, which
   * is the tidier-looking thing and the wrong one -- `ap_m68030_step.h` had
   * already written that down, and this path did it anyway. */
  ap_m68030_cache_clear(&machine->instruction_cache);
  ap_m68030_cache_clear(&machine->data_cache);

  ap_m68030_cpu_reset(&machine->cpu, pc);
}

bool ap_machine_write(ap_machine_t *machine, uint32_t address, unsigned size,
                      uint32_t value) {
  if (!in_range(machine, address, size)) {
    return false;
  }
  write_bytes(machine, address, size, value);

  /* The operator's write did not run a bus cycle, so anything the processor has
   * cached for this address is now stale. Clearing both caches is the blunt
   * answer and the right one here: a probe sets memory up before it runs, and a
   * machine that let a stale line survive would run the *old* program while
   * reporting the new one. */
  ap_m68030_cache_clear(&machine->instruction_cache);
  ap_m68030_cache_clear(&machine->data_cache);
  return true;
}

bool ap_machine_read(const ap_machine_t *machine, uint32_t address,
                     unsigned size, uint32_t *value) {
  if (!in_range(machine, address, size)) {
    return false;
  }
  *value = read_bytes(machine, address, size);
  return true;
}

ap_m68030_walk_result_t ap_machine_walk(ap_machine_t *machine, uint32_t logical,
                                        uint8_t function_code) {
  ap_m68030_cpu_t *cpu = &machine->cpu;
  ap_m68030_walk_result_t out = {0};
  if (!cpu->tc.enable || cpu->data == NULL || cpu->data->table_fetch == NULL) {
    return out;
  }
  const bool supervisor = (function_code & 0x4u) != 0u;
  const ap_m68030_root_t *root =
      (cpu->tc.supervisor_root && supervisor) ? &cpu->srp : &cpu->crp;
  const ap_m68030_search_access_t access = {
      .write = false, .read_modify_write = false, .supervisor = supervisor};
  const bool was_probing = machine->probing;
  machine->probing = true;
  out = ap_m68030_walk(&cpu->tc, root, logical, &access, cpu->data->table_fetch,
                       NULL, cpu->data->context);
  machine->probing = was_probing;
  return out;
}

bool ap_machine_translate(ap_machine_t *machine, uint32_t logical,
                          uint8_t function_code, uint32_t *physical) {
  ap_m68030_cpu_t *cpu = &machine->cpu;

  /* Transparent translation first, as an access resolves it: a matching window
   * answers without the tables and without protection checking, so asking the
   * tables first would report a mapping the processor would never have used. */
  const ap_m68030_access_t probe = {.address = logical,
                                    .function_code = function_code,
                                    .read = true,
                                    .read_modify_write = false};
  const ap_m68030_tt_result_t transparent =
      ap_m68030_tt_translate(&cpu->tt0, &cpu->tt1, &probe);
  if (transparent.transparent) {
    *physical = transparent.physical;
    return true;
  }

  if (!cpu->tc.enable) {
    *physical = logical;
    return true;
  }

  if (cpu->data == NULL || cpu->data->table_fetch == NULL) {
    return false;
  }

  const bool supervisor = (function_code & 0x4u) != 0u;
  const ap_m68030_root_t *root =
      (cpu->tc.supervisor_root && supervisor) ? &cpu->srp : &cpu->crp;
  const ap_m68030_search_access_t access = {
      .write = false, .read_modify_write = false, .supervisor = supervisor};
  /* A null `update` is what keeps this an observation: `ap_m68030_walk` takes
   * the callback precisely so PTEST -- and now this -- can search the tree
   * without setting the used and modified bits a real access would. */
  /* Marked as the observer's for the duration, so the fetches this costs are
   * charged to `probe_fetches` and not to the machine. Saved and restored
   * rather than cleared, so a probe reached from inside a probe -- which
   * nothing does today -- would still leave the flag as it found it. */
  const bool was_probing = machine->probing;
  machine->probing = true;
  const ap_m68030_walk_result_t walk =
      ap_m68030_walk(&cpu->tc, root, logical, &access, cpu->data->table_fetch,
                     NULL, cpu->data->context);
  machine->probing = was_probing;
  if (!walk.ok) {
    return false;
  }
  *physical = walk.physical;
  return true;
}

bool ap_machine_read_logical(ap_machine_t *machine, uint32_t logical,
                             uint8_t function_code, unsigned size,
                             uint32_t *value) {
  uint32_t physical = 0;
  if (!ap_machine_translate(machine, logical, function_code, &physical)) {
    return false;
  }
  /* Through the board when there is one, because a board machine's memory is
   * not at the address `ap_machine_read` would index it by: the board maps RAM
   * at `01000000` and indexes the buffer from its base, so reading
   * `machine->ram[0100D098]` reads a megabyte and a half past where the word
   * actually is. That is why the trace still printed `0000` for Domain/OS's own
   * text after the logical read was added -- the translation was right and the
   * indexing was not. */
  if (machine->board != NULL) {
    return ap_board_peek_ram(machine->board, physical, size, value);
  }
  return ap_machine_read(machine, physical, size, value);
}

ap_m68030_step_result_t ap_machine_step(ap_machine_t *machine) {
  return ap_m68030_step(&machine->cpu);
}

/* No `flatten` here, and that is a measured decision rather than an omission.
 * The plan names it as a squeeze candidate; applied to this loop it measured
 * 299 s against 296 s on a 350 M boot, inside the noise, because the release
 * build already has LTO and the profile shows the bus tick, the DMA queries,
 * the 8237 and the arbiter all inlined into one another already. A
 * compiler-specific attribute that buys nothing is complexity without a
 * reason. */
ap_machine_run_t ap_machine_run(ap_machine_t *machine, unsigned limit) {
  ap_machine_run_t out = {.status = AP_M68030_STEP_EXECUTED};

  for (unsigned i = 0; i < limit; i++) {
    const uint64_t before = machine->cpu.clocks;
    if (machine->board != NULL) {
      /* Sampled before every instruction, because the lines are levels and a
       * program that has just written a device register has changed them. This
       * is the whole of the tick loop that exists: nothing advances on its own,
       * so an interrupt appears only where a program produced one. */
      /* The coprocessor is on the bus unless the control register says
       * otherwise. Detaching it is what makes an FPU opcode take F-line, which
       * is what the board reports as an FP trap. */
      machine->cpu.fpu = ap_boardreg_fpu_trapped(&machine->board->registers)
                             ? NULL
                             : &machine->fpu;
      /* ## Skipping the sample when nothing can have changed
       *
       * The sample is level-based and costs eight device queries, and the
       * profile puts it at 8.6% of a boot -- the largest single item, run
       * unconditionally. It can be skipped exactly when two things hold: no bus
       * access has happened since the last sample, and the bound every source
       * agreed to has not been reached.
       *
       * `interrupt_valid_until` carries both. It is set to the aggregate bound
       * when a sample is taken and cleared by the three sites that can reach a
       * device -- a processor read, a processor write, and a DMA cycle -- so a
       * zero here means "no promise outstanding" and always samples. That is
       * also its value on a fresh board, which is the safe default: a machine
       * that has never sampled never skips.
       *
       * The comparison is against the time the board has been advanced to,
       * which is `machine->now`: the advance at the foot of this loop is what
       * moved the devices, and the sample above reads the state that advance
       * produced. Anything later has not happened yet.
       *
       * When the sample is skipped the level is left as it was, which is the
       * point -- no source changed, so the 8259's requests are still right, and
       * `ap_board_interrupt_level` reads them rather than recomputing. */
      if (machine->now >= machine->board->interrupt_valid_until) {
        ap_board_sample_interrupts(machine->board);
      }
      machine->cpu.interrupt_level = ap_board_interrupt_level(machine->board);

      /* The bus advances at the processor's rate, so it is given the clocks the
       * previous instruction spent. Without this the arbiter would see one
       * clock per *instruction* and a DMA controller would take as long to win
       * the bus as the program took to run.
       *
       * Charged to the board and not to the CPU: these are clocks that already
       * happened. */
      /* Batched when the board says the ticks are identical to one -- no DMA
       * able to ask and an idle arbiter -- and looped otherwise. The decision
       * lives in `ap_board_bus_ticks` because both guards are the board's. */
      ap_board_bus_ticks(machine->board, machine->last_instruction_clocks);

      /* And now the contention, which is not a penalty and not a figure: the
       * processor is the lowest-priority claimant of a bus somebody else is
       * holding (`[030]` §7.7), so it does not run, and the clocks pass anyway.
       * Nothing here computes a delay -- the loop simply cannot exit until the
       * arbiter says the processor may run.
       *
       * The guard is not a timeout in disguise. A master that never releases is
       * a broken machine, and spinning forever inside a bounded `ap_machine_run`
       * would turn that into a hung harness rather than a visible fault -- the
       * same reason the run takes a limit at all. */
      unsigned stalled = 0;
      while (!ap_board_processor_may_run(machine->board) &&
             stalled < AP_MACHINE_STALL_LIMIT) {
        ap_board_bus_tick(machine->board);
        machine->cpu.clocks++;
        stalled++;
      }
    }
    /* The vector counts before the step, so the one it takes can be told from
     * the ones it already had. Sampled rather than diffed at the end because a
     * single step takes at most one exception and the question is *which
     * instruction* raised it. */
    const unsigned watched = machine->exception_stop_vector;
    const unsigned before_taken =
        watched != 0u ? machine->cpu.exceptions_taken[watched & 0xFFu] : 0u;
    const uint32_t before_pc = machine->cpu.regs.pc;

    const ap_m68030_step_result_t result = ap_m68030_step(&machine->cpu);
    if (watched != 0u && !machine->exception_stopped &&
        machine->cpu.exceptions_taken[watched & 0xFFu] != before_taken &&
        machine->exception_stop_seen++ >= machine->exception_stop_skip) {
      machine->exception_stopped = true;
      /* Where it came *from*. The processor's PC is the handler's by now, which
       * is the same address for every cause and so answers nothing. */
      machine->exception_stop_pc = before_pc;
    }
    machine->last_instruction_clocks = machine->cpu.clocks - before;
    /* Converted once, here. The step reports CPU clocks; the machine keeps
     * time. A `cpu_clock` that was never initialised has a zero rate and
     * produces no time at all, which is visibly wrong rather than quietly
     * approximate. */
    machine->now += ap_clock_duration(&machine->cpu_clock,
                                      machine->cpu.clocks - before);

    /* And every device that keeps time advances to that instant. After the
     * step, so a device sees the effect of an instruction that programmed it
     * before it counts; before the next iteration's interrupt sample, so
     * anything it raises is seen on the next instruction rather than the one
     * after. */
    if (machine->board != NULL) {
      ap_board_advance(machine->board, machine->now);

      /* An F-line taken while the coprocessor is held off is the FP trap the
       * status register reports. Counted rather than signalled, because the
       * step result carries no vector and a count is the observable this core
       * already keeps. */
      const unsigned line_f =
          machine->cpu.exceptions_taken[AP_M68030_VECTOR_LINE_F];
      if (line_f != machine->last_line_f_exceptions &&
          ap_boardreg_fpu_trapped(&machine->board->registers)) {
        ap_boardreg_latch_status(&machine->board->registers,
                                 AP_BOARDREG_STATUS_FP_TRAP);
      }
      machine->last_line_f_exceptions = line_f;
    }
    out.status = result.status;
    out.instruction = result.instruction;

    /* An exception is progress: the handler runs next. Everything else that is
     * not EXECUTED is the processor declining to go on, and a probe wants to
     * know that rather than spin to its limit. */
    if (result.status != AP_M68030_STEP_EXECUTED &&
        result.status != AP_M68030_STEP_EXCEPTION) {
      return out;
    }
    out.executed++;
  }
  return out;
}

static uint64_t machine_hash_into(ap_hash_t *stp, const ap_machine_t *machine);

/* The dump and the hash are the same traversal: this is `ap_machine_hash` with
 * an output attached, so a field cannot appear in one and not the other. It
 * returns the hash as well, which is the check that the two agree -- a dump
 * whose hash differs from an ordinary run's is a dump of a different walk. */
uint64_t ap_machine_dump_state(const ap_machine_t *machine, void *out) {
  ap_hash_t st = ap_hash_begin();
  ap_hash_dump_to(&st, out);
  return machine_hash_into(&st, machine);
}

uint64_t ap_machine_hash(const ap_machine_t *machine) {
  ap_hash_t st = ap_hash_begin();
  return machine_hash_into(&st, machine);
}

static uint64_t machine_hash_into(ap_hash_t *stp, const ap_machine_t *machine) {
  ap_hash_t st = *stp;
  ap_hash_scope(&st, "cpu");
  ap_m68030_hash_cpu(&st, &machine->cpu);
  ap_hash_scope(&st, "memory");

  /* The RAM too: a run that left different memory behind is a different run
   * however well its registers agree. */
  ap_hash_u32(&st, machine->ram_bytes);
  ap_hash_bytes(&st, machine->ram, machine->ram_bytes);

  /* The board's devices, when there is a board. Absence is a marker rather than
   * nothing, so a probe machine on flat RAM does not hash as a DN3500 whose
   * every device happens to be at reset. */
  if (machine->board == NULL) {
    ap_hash_u8(&st, 0xFFu);
  } else {
    ap_hash_u8(&st, 0x01u);
    ap_board_hash(&st, machine->board);
  }

  /* The machine's clock, and the rate it runs at. `ap_machine_now` is the
   * quantity every device will advance against once the tick loop exists, so a
   * hash that omitted it would stop covering timing the moment a device kept
   * time of its own. The rate is state as much as the elapsed time is: a
   * machine at 25 MHz that has produced the same number of base units as one at
   * 20 MHz has run a different number of cycles to get there. */
  ap_hash_time(&st, machine->now);
  ap_hash_u32(&st, machine->cpu_clock.hz);

  /* `bus_errors` is deliberately absent -- see ap_machine.h. It is reported by
   * `ap_machine_state` instead. */
  return ap_hash_end(&st);
}

ap_machine_state_t ap_machine_state(const ap_machine_t *machine) {
  return (ap_machine_state_t){
      .hash = ap_machine_hash(machine),
      .clocks = machine->cpu.clocks,
      .now = machine->now,
      .pc = machine->cpu.regs.pc,
      .bus_errors = machine->bus_errors,
      .first_bus_error = machine->first_bus_error,
      .last_bus_error = machine->last_bus_error,
      .last_bus_error_pc = machine->last_bus_error_pc,
      .table_fetches = machine->table_fetches,
      .table_updates = machine->table_updates,
      .probe_fetches = machine->probe_fetches,
  };
}

void ap_machine_set_board(ap_machine_t *machine, struct ap_board *board) {
  machine->board = board;
}

ap_time_t ap_machine_now(const ap_machine_t *machine) { return machine->now; }

