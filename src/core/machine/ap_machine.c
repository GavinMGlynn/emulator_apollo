/* A constructed machine. See ap_machine.h for why it exists and what it is not. */

#include "machine/ap_machine.h"

#include "cpu/m68030/ap_m68030_state.h"
#include "board/ap_board.h"

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
  /* Every field, not merely the ones set below. A machine is a value the caller
   * creates -- usually on the stack -- and one whose behaviour depended on what
   * was in that memory beforehand would be a machine that is not reproducible,
   * which is the one thing this must be. The cache and ATC resets below clear
   * valid bits rather than data, by design, so without this the leftovers would
   * be whatever the caller happened to have. */
  *machine = (ap_machine_t){0};

  machine->ram = ram;
  machine->ram_bytes = ram_bytes;
  machine->bus_errors = 0;

  ap_m68030_cache_clear(&machine->instruction_cache);
  ap_m68030_cache_clear(&machine->data_cache);
  ap_m68030_atc_flush(&machine->atc);

  machine->cpu = (ap_m68030_cpu_t){0};

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
      .context = machine,
  };
  machine->data_access = machine->instruction_access;
  machine->data_access.cache = &machine->data_cache;

  machine->cpu.fetch.access = &machine->instruction_access;
  machine->cpu.fetch.function_code = AP_M68030_FC_SUPERVISOR_PROGRAM;
  machine->cpu.data = &machine->data_access;
  machine->cpu.data_function_code = AP_M68030_FC_SUPERVISOR_DATA;
}

void ap_machine_reset(ap_machine_t *machine, uint32_t pc, uint32_t stack) {
  /* "Supervisor state with interrupts masked at 7, which is what reset
   * leaves." The trace bits are clear with it. */
  ap_m68030_write_sr(&machine->cpu.regs,
                     (uint16_t)((1u << AP_M68030_SR_S_BIT) |
                                (AP_M68030_SR_INTERRUPT_MASK
                                 << AP_M68030_SR_INTERRUPT_SHIFT)));
  machine->cpu.regs.isp = stack;
  machine->cpu.stopped = false;
  machine->cpu.pending_vector = 0;
  machine->cpu.interrupt_level = 0;
  machine->cpu.previous_interrupt_level = 0;
  machine->cpu.clocks = 0;
  machine->bus_errors = 0;

  ap_m68030_cache_clear(&machine->instruction_cache);
  ap_m68030_cache_clear(&machine->data_cache);
  ap_m68030_atc_flush(&machine->atc);

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
    const ap_m68030_step_result_t result = ap_m68030_step(&machine->cpu);
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
  ap_hash_u32(&st, (uint32_t)machine->bus_errors);
  return ap_hash_end(&st);
}

void ap_machine_set_board(ap_machine_t *machine, struct ap_board *board) {
  machine->board = board;
}
