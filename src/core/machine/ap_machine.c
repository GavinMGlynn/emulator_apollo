/* A constructed machine. See ap_machine.h for why it exists and what it is not. */

#include "machine/ap_machine.h"

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

/* With a board attached, an access is a board access -- byte at a time, because
 * the map is a map of devices and a device answers its own registers. `ok` is
 * false if *any* byte went unanswered: a long word half in a device and half in
 * nothing is not a transfer this machine can make. */
static bool board_read(ap_machine_t *machine, uint32_t address, unsigned count,
                       uint32_t *out) {
  uint32_t value = 0;
  for (unsigned i = 0; i < count; i++) {
    bool ok = false;
    uint8_t byte = ap_board_read(machine->board, address + i, &ok);
    if (!ok) {
      return false;
    }
    value = (value << 8) | byte;
  }
  *out = value;
  return true;
}

static bool board_write(ap_machine_t *machine, uint32_t address, unsigned count,
                        uint32_t value) {
  bool all = true;
  for (unsigned i = 0; i < count; i++) {
    bool ok = false;
    ap_board_write(machine->board, address + i,
                   (uint8_t)(value >> ((count - 1u - i) * 8u)), &ok);
    all = all && ok;
  }
  return all;
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
      machine->bus_errors++;
      out->termination = AP_M68030_TERM_BERR;
      out->burst_acknowledge = false;
      return;
    }
    out->termination = AP_M68030_TERM_STERM;
    out->burst_acknowledge = false;
    out->data[0] = value;
    return;
  }

  if (!in_range(machine, line_address, 4u)) {
    /* "Outside the RAM is a bus error, not a wrap and not a zero." */
    machine->bus_errors++;
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
  (void)level;
  ap_machine_t *machine = (ap_machine_t *)context;
  ap_m68030_iack_t out = {0};
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

static bool machine_store(void *context, uint32_t physical, uint32_t value,
                          unsigned size) {
  ap_machine_t *machine = (ap_machine_t *)context;

  if (machine->board != NULL) {
    if (!board_write(machine, physical, size, value)) {
      machine->bus_errors++;
      return false;
    }
    return true;
  }

  if (!in_range(machine, physical, size)) {
    machine->bus_errors++;
    return false;
  }
  write_bytes(machine, physical, size, value);
  return true;
}

/* The table search's descriptor fetch, over the same RAM. A machine whose MMU
 * is off never calls this; one whose tables a probe has built does. */
static bool machine_table_fetch(void *context, uint32_t physical,
                                bool long_format,
                                ap_m68030_descriptor_t *out) {
  ap_machine_t *machine = (ap_machine_t *)context;
  const unsigned words = long_format ? 2u : 1u;

  if (!in_range(machine, physical, words * 4u)) {
    machine->bus_errors++;
    return false; /* "Returns false for a bus error", which sets B in the ATC */
  }

  const uint32_t upper = read_bytes(machine, physical, 4u);
  if (!long_format) {
    *out = ap_m68030_descriptor_unpack_short(upper, false);
    return true;
  }
  *out = ap_m68030_descriptor_unpack_long(upper, read_bytes(machine, physical + 4u, 4u),
                                          false);
  return true;
}

/* The history-bit update, which is the write half of a read-modify-write. The
 * bits live in the descriptor's status field: U is bit 3 of a short descriptor
 * and of a long one's lower word, M is bit 4. */
static bool machine_table_update(void *context, uint32_t physical,
                                 bool set_used, bool set_modified) {
  ap_machine_t *machine = (ap_machine_t *)context;

  if (!in_range(machine, physical, 4u)) {
    machine->bus_errors++;
    return false;
  }

  uint32_t descriptor = read_bytes(machine, physical, 4u);
  if (set_used) {
    descriptor |= UINT32_C(1) << 3;
  }
  if (set_modified) {
    descriptor |= UINT32_C(1) << 4;
  }
  write_bytes(machine, physical, 4u, descriptor);
  return true;
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
      .translation_enabled = false,
      .fill = machine_fill,
      .store = machine_store,
      .table_fetch = machine_table_fetch,
      .table_update = machine_table_update,
      /* Always supplied, and it answers zero until a board is attached: a probe
       * on flat RAM has no device with a published cycle time, so nothing it
       * measures moves. Wiring it here rather than in `ap_machine_set_board`
       * keeps one construction path -- a callback installed by a setter is a
       * callback some caller forgets. */
      .wait_states = machine_wait_states,
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

ap_m68030_step_result_t ap_machine_step(ap_machine_t *machine) {
  return ap_m68030_step(&machine->cpu);
}

ap_machine_run_t ap_machine_run(ap_machine_t *machine, unsigned limit) {
  ap_machine_run_t out = {.status = AP_M68030_STEP_EXECUTED};

  for (unsigned i = 0; i < limit; i++) {
    const uint64_t before = machine->cpu.clocks;
    /* Sampled before every instruction, because the lines are levels and a
     * program that has just written a device register has changed them. This
     * is the whole of the tick loop that exists: nothing advances on its own,
     * so an interrupt appears only where a program produced one. */
    if (machine->board != NULL) {
      ap_board_sample_interrupts(machine->board);
      machine->cpu.interrupt_level = ap_board_interrupt_level(machine->board);
    }
    const ap_m68030_step_result_t result = ap_m68030_step(&machine->cpu);
    /* Converted once, here. The step reports CPU clocks; the machine keeps
     * time. A `cpu_clock` that was never initialised has a zero rate and
     * produces no time at all, which is visibly wrong rather than quietly
     * approximate. */
    machine->now += ap_clock_duration(&machine->cpu_clock,
                                      machine->cpu.clocks - before);
    out.status = result.status;

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

uint64_t ap_machine_hash(const ap_machine_t *machine) {
  ap_hash_t st = ap_hash_begin();
  ap_m68030_hash_cpu(&st, &machine->cpu);

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
  };
}

void ap_machine_set_board(ap_machine_t *machine, struct ap_board *board) {
  machine->board = board;
}

ap_time_t ap_machine_now(const ap_machine_t *machine) { return machine->now; }
