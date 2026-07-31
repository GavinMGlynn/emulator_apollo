/* Probes. See ap_probe.h for why the results are goldens rather than
 * assertions, and what the clock in a result does and does not include. */

#include "probe/ap_probe.h"

#include "machine/ap_machine.h"

/* Every probe loads and runs at the same place, so the goldens differ only by
 * what the probe does rather than by where it sat. */
#define PROBE_LOAD 0x00001000u
#define PROBE_STACK 0x00009000u
#define PROBE_DATA 0x00002000u

/* --- The probes -----------------------------------------------------------
 *
 * Each is small enough to read at a glance and covers one thing the golden
 * would notice a change in. They are deliberately not exhaustive: the unit
 * suites test behaviour, and these pin *identity* across platforms and builds.
 */

/* Every probe ends with `STOP #$2700`, so it finishes because the program said
 * so rather than because it ran out of limit. Without a terminator a probe runs
 * whatever follows it -- zeroes, or the next probe's leftovers -- and its
 * "instructions executed" is just the limit, which says nothing. Two of these
 * were built without one and their goldens showed it: a loop that reported 20
 * instructions for six iterations of work, and a subroutine that returned and
 * then fell into its own callee.
 *
 * `STOP` is privileged and these run in supervisor state, which is what reset
 * leaves. The mask it loads is $2700: supervisor, all interrupts masked, which
 * is the state the probe was already in. */
#define STOP_WORDS 0x4E72u, 0x2700u

/* MOVEQ #$42,D0 -- the smallest thing that can be wrong. If this probe's hash
 * moves, something very general did. */
static const uint16_t probe_moveq[] = {0x7042u, STOP_WORDS};

/* A store and a reload through memory, so the data cache, the writethrough path
 * and the operand layer all appear in one number. */
static const uint16_t probe_store_reload[] = {
    0x7042u,                   /* MOVEQ #$42,D0        */
    0x23C0u, 0x0000u, 0x2000u, /* MOVE.L D0,$2000      */
    0x2239u, 0x0000u, 0x2000u, /* MOVE.L $2000,D1      */
    STOP_WORDS,
};

/* A countdown loop: six iterations of DBF, which exercises the branch path and
 * the instruction cache answering a second time round. */
static const uint16_t probe_loop[] = {
    0x7205u,          /* MOVEQ #5,D1     */
    0x5200u,          /* ADDQ.B #1,D0    */
    0x51C9u, 0xFFFCu, /* DBF D1,-4       */
    STOP_WORDS,
};

/* A subroutine call and return: the stack working in both directions and the
 * return address landing where the caller expects. The callee sits *after* the
 * terminator, so returning stops rather than falling into it -- which is the
 * mistake the first version of this probe made. */
static const uint16_t probe_subroutine[] = {
    0x6100u, 0x0006u, /* BSR.W +6 -> the callee below */
    STOP_WORDS,       /* returned here, and stops     */
    0x7007u,          /* MOVEQ #7,D0                  */
    0x4E75u,          /* RTS                          */
};

/* A TRAP taken and returned from: the frame stacked on the supervisor stack,
 * the vector fetched through the VBR, and RTE putting everything back. The
 * handler is the one the runner plants for every vector. */
static const uint16_t probe_trap[] = {
    0x4E40u, /* TRAP #0          */
    0x7009u, /* MOVEQ #9,D0      */
    STOP_WORDS,
};

/* The multiplies and divides, whose results are wide enough that a wrong
 * operand width shows up in the hash rather than hiding in a low byte. */
static const uint16_t probe_wide_arithmetic[] = {
    0x203Cu, 0x0000u, 0x1234u, /* MOVE.L #$1234,D0  */
    0xC0FCu, 0x0010u,          /* MULU.W #$10,D0    */
    0x80FCu, 0x0007u,          /* DIVU.W #7,D0      */
    STOP_WORDS,
};

/* MOVEM saving and restoring a register set through the stack, which is the
 * predecrement mask reversal in a form a golden would notice. */
static const uint16_t probe_movem[] = {
    0x7001u,          /* MOVEQ #1,D0            */
    0x7202u,          /* MOVEQ #2,D1            */
    0x48E7u, 0xC000u, /* MOVEM.L D0-D1,-(A7)    */
    0x7003u,          /* MOVEQ #3,D0            */
    0x7204u,          /* MOVEQ #4,D1            */
    0x4CDFu, 0x0003u, /* MOVEM.L (A7)+,D0-D1    */
    STOP_WORDS,
};

/* PMOVE loading the translation control register, so the MMU registers appear
 * in a golden even though nothing translates through them yet. */
static const uint16_t probe_pmove[] = {
    0x207Cu, 0x0000u, 0x2000u, /* MOVEA.L #$2000,A0 */
    0xF010u, 0x4000u,          /* PMOVE (A0),TC     */
    STOP_WORDS,
};

#define PROBE(field_name, program, purpose_text, instruction_limit)            \
  {                                                                            \
      .name = field_name,                                                      \
      .purpose = purpose_text,                                                 \
      .words = program,                                                        \
      .word_count = (unsigned)(sizeof(program) / sizeof((program)[0])),        \
      .load_address = PROBE_LOAD,                                              \
      .entry = PROBE_LOAD,                                                     \
      .stack = PROBE_STACK,                                                    \
      .limit = instruction_limit,                                              \
  }

static const ap_probe_t PROBES[] = {
    PROBE("moveq", probe_moveq, "one register write, the smallest thing that can be wrong", 20),
    PROBE("store-reload", probe_store_reload, "operand write-through and read-back", 20),
    PROBE("loop", probe_loop, "a DBcc countdown, and the instruction cache on the second pass", 40),
    PROBE("subroutine", probe_subroutine, "BSR and RTS, the stack in both directions", 20),
    PROBE("trap", probe_trap, "TRAP taken, handler entered and returned from", 20),
    PROBE("wide-arithmetic", probe_wide_arithmetic, "MULU then DIVU, both halves of the register", 20),
    PROBE("movem", probe_movem, "MOVEM out and back, the predecrement mask reversal", 20),
    PROBE("pmove", probe_pmove, "PMOVE into the translation control register", 20),
};

const ap_probe_t *ap_probe_all(unsigned *count) {
  *count = (unsigned)(sizeof(PROBES) / sizeof(PROBES[0]));
  return PROBES;
}

const char *ap_probe_status_name(ap_m68030_step_status_t status) {
  switch (status) {
  case AP_M68030_STEP_EXECUTED:
    return "EXECUTED";
  case AP_M68030_STEP_UNIMPLEMENTED:
    return "UNIMPLEMENTED";
  case AP_M68030_STEP_ILLEGAL:
    return "ILLEGAL";
  case AP_M68030_STEP_FAULT:
    return "FAULT";
  case AP_M68030_STEP_EXCEPTION:
    return "EXCEPTION";
  case AP_M68030_STEP_STOPPED:
    return "STOPPED";
  }
  /* Not reachable while the switch is exhaustive, and -Wswitch-enum keeps it
   * so; a name is still returned rather than a null a formatter would print as
   * "(null)" or crash on. */
  return "UNKNOWN";
}

ap_probe_result_t ap_probe_run(const ap_probe_t *probe, uint8_t *ram,
                               uint32_t ram_bytes) {
  ap_probe_result_t out = {0};

  /* Blanked first: a probe's result must not depend on what the previous probe
   * left behind, or the suite's answer would depend on its order. */
  for (uint32_t i = 0; i < ram_bytes; i++) {
    ram[i] = 0;
  }

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
  ap_machine_reset(&machine, probe->entry, probe->stack);

  for (unsigned i = 0; i < probe->word_count; i++) {
    (void)ap_machine_write(&machine, probe->load_address + i * 2u, 2u,
                           probe->words[i]);
  }

  /* Every probe gets the same exception table: a handler that simply returns,
   * planted at a fixed address, with every vector pointing at it. A probe that
   * faults unexpectedly then reports EXECUTED from the handler rather than
   * running off into blank memory, which is a far more legible failure. */
  (void)ap_machine_write(&machine, PROBE_DATA + 0x100u, 2u, 0x4E73u); /* RTE */
  for (unsigned vector = 2; vector < 64u; vector++) {
    (void)ap_machine_write(&machine, vector * 4u, 4u, PROBE_DATA + 0x100u);
  }

  const ap_machine_run_t run = ap_machine_run(&machine, probe->limit);

  out.executed = run.executed;
  out.status = run.status;
  out.clocks = machine.cpu.clocks;
  out.bus_errors = machine.bus_errors;
  out.d0 = machine.cpu.regs.d[0];
  out.pc = machine.cpu.regs.pc;
  out.hash = ap_machine_hash(&machine);
  return out;
}


/* --- Per-instruction timing ------------------------------------------------
 *
 * See ap_probe.h for why this measures consecutive deltas rather than a single
 * step, and why an unsteady result is reported rather than averaged.
 */

/* The instructions timed, and what they are. Single-word forms only: a
 * multi-word instruction would need its extension words laid down too, and the
 * point here is a like-for-like comparison rather than coverage. */
static const struct {
  uint16_t word;
  const char *mnemonic;
} TIMED[] = {
    {0x4E71u, "NOP"},
    {0x7042u, "MOVEQ #$42,D0"},
    {0xD280u, "ADD.L D0,D1"},
    {0xE288u, "LSR.L #1,D0"},
    {0x4A80u, "TST.L D0"},
    {0x2200u, "MOVE.L D0,D1"},
    {0x4840u, "SWAP D0"},
    {0xB280u, "CMP.L D0,D1"},
};

static ap_probe_timing_t TIMED_RESULTS[sizeof TIMED / sizeof TIMED[0]];

ap_probe_timing_t ap_probe_time_instruction(uint16_t word,
                                            const char *mnemonic, uint8_t *ram,
                                            uint32_t ram_bytes) {
  ap_probe_timing_t out = {.word = word, .mnemonic = mnemonic, .ok = true};

  for (uint32_t i = 0; i < ram_bytes; i++) {
    ram[i] = 0;
  }

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
  ap_machine_reset(&machine, PROBE_LOAD, PROBE_STACK);

  /* One copy per step, plus the discarded first one, plus a margin so the
   * prefetch never reads past what was laid down. */
  const unsigned copies = AP_PROBE_TIMING_SAMPLES + 8u;
  for (unsigned i = 0; i < copies; i++) {
    (void)ap_machine_write(&machine, PROBE_LOAD + i * 2u, 2u, word);
  }

  /* The first step pays for filling the pipe, so it is taken and discarded --
   * exactly as the oracle's harness discards its first interval. */
  if (ap_machine_step(&machine).status != AP_M68030_STEP_EXECUTED) {
    out.ok = false;
    return out;
  }

  uint64_t previous = machine.cpu.clocks;
  for (unsigned i = 0; i < AP_PROBE_TIMING_SAMPLES; i++) {
    if (ap_machine_step(&machine).status != AP_M68030_STEP_EXECUTED) {
      out.ok = false;
      return out;
    }
    out.delta[i] = (uint32_t)(machine.cpu.clocks - previous);
    previous = machine.cpu.clocks;
    out.samples++;
  }

  out.steady = true;
  for (unsigned i = 1; i < out.samples; i++) {
    if (out.delta[i] != out.delta[0]) {
      out.steady = false;
    }
  }
  return out;
}

const ap_probe_timing_t *ap_probe_timed_instructions(unsigned *count) {
  *count = (unsigned)(sizeof TIMED / sizeof TIMED[0]);
  for (unsigned i = 0; i < *count; i++) {
    TIMED_RESULTS[i].word = TIMED[i].word;
    TIMED_RESULTS[i].mnemonic = TIMED[i].mnemonic;
  }
  return TIMED_RESULTS;
}
