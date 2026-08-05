/* Deterministic frontend: no wall clock, no host input, no threads.
 *
 * Everything shared with the interactive frontend -- naming, the model table
 * report, and the options both accept -- lives in ../common. This file holds
 * only what is specific to running a machine headlessly.
 *
 * Presently it can only report the machine configuration it would build. The
 * flags that make this the engine of the verification methodology (run N
 * cycles, dump state, --dump-mem, screenshots, scripted input, ring trace) are
 * added alongside the subsystems they observe. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ap_frontend.h"
#include "probe/ap_probe.h"

#include <stdlib.h>

#include "device/ap_qic.h"
#include "image/ap_ct.h"
#include "image/ap_afd.h"
#include "image/ap_awd.h"
#include "image/ap_volume.h"
#include "board/ap_board.h"
#include "board/ap_sio.h"
#include "device/ap_mc68681.h"
#include "board/ap_graphics.h"
#include "ap_png.h"
#include "device/ap_bt458.h"
#include "device/ap_kbd.h"
#include "machine/ap_machine.h"

static void print_usage(const char *program_name) {
  ap_print_common_usage(stdout, program_name);
  /* Headless-only flags are listed here as they are implemented. */
  fprintf(stdout,
          "  --run-probes          run the built-in probe suite and report\n"
          "  --probe-file PATH     run one probe described by a file, so a\n"
          "                        probe can come from outside this binary\n"
          "  --time-instructions   report per-instruction clocks, for oracle\n"
          "                        comparison\n"
          "  --boot-limit N        stop a boot after N instructions, to find\n"
          "                        where one goes wrong\n"
          "  --boot-stop-pc ADDR   stop the run the first time the program\n"
          "                        counter is ADDR, so a kept trace holds what\n"
          "                        led there rather than what followed\n"
          "  --boot-trace-last N   keep the last N steps and print them when\n"
          "                        the run ends. A fault half a billion\n"
          "                        instructions in cannot be reached by\n"
          "                        printing every step\n"
          "  --boot-trace          report pc and a7 per step: a7 is where a\n"
          "                        stack goes wrong, pc only where it shows\n"
          "  --boot-watch ADDR     with --boot-trace, report the long word at\n"
          "                        ADDR after every step. MEMORY ONLY: it reads\n"
          "                        through the board, so a device register\n"
          "                        would be perturbed by being watched\n"
          "  --boot-input TEXT     deliver TEXT to serial port 2 as the\n"
          "                        firmware reads it; scripted, not host input\n"
          "  --boot-console        print what the machine transmits on either\n"
          "                        serial port: its own console output\n"
          "  --boot-input-port N   which serial port --boot-input feeds, 1 or\n"
          "                        2 (default 2)\n"
          "  --boot-input-rate CSR clock select the scripted terminal sends at\n"
          "                        (default 0xBB, 9600 baud, which is what\n"
          "                        makes the boot PROM answer)\n"
          "  --boot-input-interval US  emulated microseconds between scripted\n"
          "                        characters; the wire's own floor if 0\n"
          "  --boot-key N          press and release keyboard key N (a matrix\n"
          "                        index 0-7F, not a character)\n"
          "  --screen KIND         fit a display: c4p, c8p, 19i or 15i\n"
          "  --screenshot FILE     scan the fitted screen out to a PNG\n"
          "  --disk FILE           fit a Winchester (.awd) to the boot\n"
          "  --dump-mem A[:L]      dump memory through the board after a\n"
          "                        run; hex address, hex length (default 100)\n"
          "  --floppy FILE         read an .afd through the reader and\n"
          "                        report its geometry\n"
          "  --boot-input-channel C  which channel, A or B (default A). The\n"
          "                        keyboard is port 1 channel A; a terminal is\n"
          "                        port 1 channel B\n");
}

/* The probes' RAM. Static rather than automatic because it is large, and
 * supplied by the caller because the core allocates nothing. */
#define PROBE_RAM_BYTES 0x00010000u
static uint8_t probe_ram[PROBE_RAM_BYTES];

/* Per-instruction clocks, measured the way tools/mame-oracle/steptime.lua
 * measures the oracle's -- consecutive deltas after a discarded first step, so
 * neither side charges one instruction for filling the pipe.
 *
 * This report is *not* a claim about the hardware. It is one half of a
 * comparison, and the other half is the oracle's; the manual arbitrates. The
 * header says so, because a table of numbers with no such note is exactly what
 * gets quoted later as if it were measured silicon. */
static void time_instructions(FILE *out) {
  unsigned count = 0;
  const ap_probe_timing_t *timed = ap_probe_timed_instructions(&count);

  fprintf(out, "# apollo per-instruction clocks\n");
  fprintf(out,
          "# BUS AND CACHE TIME ONLY. Instruction execution time -- the\n"
          "# microcode clocks between the bus cycles -- is not yet modelled, so\n"
          "# every figure here is a lower bound. Compare against the oracle with\n"
          "# tools/mame-oracle/steptime.lua, and against MC68030 User's Manual\n"
          "# ch. 11; classify each disagreement rather than moving these to fit.\n"
          "#\n"
          "# The alternation below is expected, not noise. The cache holding\n"
          "# register is a long word, so one external fetch serves two\n"
          "# instruction words: [030] 11.3.3, \"one external bus cycle per two\n"
          "# instruction prefetches\". The published tables average the odd- and\n"
          "# even-aligned cases; this reports both.\n");
  fprintf(out, "%-6s %-18s %-7s %s\n", "word", "instruction", "steady",
          "clocks per step");

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_timing_t result = ap_probe_time_instruction(
        timed[i].word, timed[i].mnemonic, probe_ram, PROBE_RAM_BYTES);

    fprintf(out, "%04X   %-18s ", result.word, result.mnemonic);
    if (!result.ok) {
      fprintf(out, "%-7s (did not execute)\n", "-");
      continue;
    }
    fprintf(out, "%-7s", result.steady ? "yes" : "NO");
    for (unsigned s = 0; s < result.samples; s++) {
      fprintf(out, " %u", result.delta[s]);
    }
    fprintf(out, "\n");
  }
}

/* A fixed-width report, because it is read as a golden diff by a person: a
 * column that moves when one field widens turns a one-line change into a
 * whole-file one. */
static void run_probes(FILE *out, ap_model_id_t model) {
  unsigned count = 0;
  const ap_probe_t *probes = ap_probe_all(&count);

  fprintf(out, "# apollo probe results\n");
  fprintf(out,
          "# clocks cover bus and cache time only: instruction execution time\n"
          "# is not yet modelled, so these are lower bounds rather than\n"
          "# figures to compare against MC68030 User's Manual ch. 11.\n");
  fprintf(out, "%-18s %4s %-14s %-8s %-8s %8s %4s %-16s\n", "probe", "ran",
          "status", "d0", "pc", "clocks", "berr", "hash");

  for (unsigned i = 0; i < count; i++) {
    const ap_probe_result_t result =
        ap_probe_run(&probes[i], probe_ram, PROBE_RAM_BYTES, model);
    fprintf(out, "%-18s %4u %-14s %08X %08X %8llu %4u %016llX\n",
            probes[i].name, result.executed,
            ap_probe_status_name(result.status), (unsigned)result.d0,
            (unsigned)result.pc, (unsigned long long)result.clocks,
            result.bus_errors, (unsigned long long)result.hash);
  }

  /* What each probe is for, printed once below the table rather than in it:
   * a golden diff shows the numbers, and the purposes are what make a moved
   * number interpretable by someone who did not write it. */
  fprintf(out, "\n# what each probe covers\n");
  for (unsigned i = 0; i < count; i++) {
    fprintf(out, "# %-18s %s\n", probes[i].name, probes[i].purpose);
  }
}

/* Run one probe described by a file, so a probe can come from outside this
 * binary.
 *
 * `tools/mame-oracle/encoder.py` hand-assembles the words and
 * `tools/mame-oracle/probe.lua` writes the same words into the oracle's RAM, so
 * the two sides run the identical program by construction rather than by two
 * people transcribing it. Neither path needs firmware or a boot.
 *
 * The sentinel is read back from the RAM this frontend owns rather than
 * returned by `ap_probe_run`: a probe result reports registers, and a probe
 * that proves a *store* has to be checked where the store landed.
 */
/* Dump a range of the machine's memory, through the **board** rather than out of
 * the buffer.
 *
 * Through the board because that is what a program sees: an address the board
 * decodes to a device answers with the device's value, and one it decodes to
 * nothing is *reported* rather than silently read as zero. A dump that indexed
 * the RAM array would show a frame buffer as blank and an unmapped hole as
 * plausible data.
 *
 * The format is one line per sixteen bytes: address, the bytes in hex, then the
 * printable characters. Bytes nothing answered print as `--` rather than `00`,
 * because "the board declined this address" and "the board answered zero" are
 * different facts and a dump that spelt them alike would be lying in the one
 * place a dump is read most carefully. */
static void dump_memory(FILE *out, ap_board_t *board, uint32_t address,
                        uint32_t length) {
  for (uint32_t offset = 0; offset < length; offset += 16u) {
    const uint32_t base = address + offset;
    const uint32_t run = (length - offset) < 16u ? (length - offset) : 16u;
    uint8_t bytes[16];
    bool answered[16];
    for (uint32_t i = 0; i < run; i++) {
      bool ok = false;
      bytes[i] = ap_board_read(board, base + i, &ok);
      answered[i] = ok;
    }
    fprintf(out, "%08X ", base);
    for (uint32_t i = 0; i < 16u; i++) {
      if (i == 8u) { fputc(' ', out); }
      if (i < run) {
        if (answered[i]) {
          fprintf(out, " %02X", bytes[i]);
        } else {
          fputs(" --", out);
        }
      } else {
        fputs("   ", out);
      }
    }
    fputs("  ", out);
    for (uint32_t i = 0; i < run; i++) {
      const uint8_t b = bytes[i];
      fputc(answered[i] && b >= 0x20u && b < 0x7Fu ? (int)b : '.', out);
    }
    fputc('\n', out);
  }
}

/* `ADDR` or `ADDR:LEN`, both hexadecimal, the length defaulting to 256 bytes.
 * Returns false for a spec that is not one, so a mistyped flag is refused
 * rather than dumping from address zero. */
static bool parse_dump_spec(const char *spec, uint32_t *address,
                            uint32_t *length) {
  char *end = NULL;
  const unsigned long start = strtoul(spec, &end, 16);
  if (end == spec) {
    return false;
  }
  *address = (uint32_t)start;
  *length = 256u;
  if (*end == ':') {
    const char *rest = end + 1;
    const unsigned long count = strtoul(rest, &end, 16);
    if (end == rest || count == 0u) {
      return false;
    }
    *length = (uint32_t)count;
  } else if (*end != '\0') {
    return false;
  }
  return true;
}

static int run_probe_file(FILE *out, ap_model_id_t model,
                          const char *program_name, const char *path,
                          const char *dump_spec) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    fprintf(stderr, "%s: cannot open probe file %s\n", program_name, path);
    return 2;
  }

  uint16_t words[256];
  unsigned word_count = 0;
  uint32_t load = 0, entry = 0, stack = 0, read_at = 0;
  unsigned limit = 64;
  bool have_read = false;
  /* Run against a whole core board rather than flat RAM. A probe that only
   * touches memory does not care; one that touches a device register cannot
   * work without it, because on flat RAM the register is simply unmapped and
   * the probe faults where the oracle's machine answers. That is what kept
   * every device verification line -- interrupt ordering, DMA transfers, timer
   * self-timing -- from having a route.
   *
   * No boot PROM is attached: `ap_board_init` does not need one, and a probe is
   * side-loaded precisely so that no firmware runs. */
  bool on_board = false;

  char line[1024];
  while (fgets(line, (int)sizeof line, file) != NULL) {
    char *cursor = line;
    while (*cursor == ' ' || *cursor == '\t') { cursor++; }
    if (*cursor == '#' || *cursor == '\n' || *cursor == '\0') { continue; }

    if (strncmp(cursor, "words", 5) == 0) {
      cursor += 5;
      while (*cursor != '\0' && *cursor != '\n') {
        while (*cursor == ' ' || *cursor == '\t') { cursor++; }
        if (*cursor == '\0' || *cursor == '\n') { break; }
        char *end = NULL;
        const unsigned long value = strtoul(cursor, &end, 16);
        if (end == cursor) { break; }
        if (word_count >= sizeof words / sizeof words[0]) {
          fprintf(stderr, "%s: probe has more than %zu words\n", program_name,
                  sizeof words / sizeof words[0]);
          fclose(file);
          return 2;
        }
        words[word_count++] = (uint16_t)(value & 0xFFFFu);
        cursor = end;
      }
      continue;
    }

    char key[32];
    unsigned long value = 0;
    if (sscanf(cursor, "%31s %lx", key, &value) != 2) { continue; }
    if (strcmp(key, "load") == 0) { load = (uint32_t)value; }
    else if (strcmp(key, "entry") == 0) { entry = (uint32_t)value; }
    else if (strcmp(key, "stack") == 0) { stack = (uint32_t)value; }
    else if (strcmp(key, "limit") == 0) { limit = (unsigned)value; }
    else if (strcmp(key, "read") == 0) { read_at = (uint32_t)value; have_read = true; }
    else if (strcmp(key, "board") == 0) { on_board = value != 0u; }
  }
  fclose(file);

  if (word_count == 0) {
    fprintf(stderr, "%s: probe file %s has no words\n", program_name, path);
    return 2;
  }

  const ap_probe_t probe = {
      .name = "file",
      .purpose = "a probe supplied from outside this binary",
      .words = words,
      .word_count = word_count,
      .load_address = load,
      .entry = entry,
      .stack = stack,
      .limit = limit,
  };
  ap_probe_result_t result;
  /* The board machine owns its RAM for the whole run, so both live until the
   * result has been reported. */
  uint8_t *board_ram = nullptr;
  ap_board_t *board = nullptr;
  ap_machine_t board_machine;

  if (!on_board) {
    result = ap_probe_run(&probe, probe_ram, PROBE_RAM_BYTES, model);
  } else {
    const ap_model_t *entry_model = ap_model_by_id(model);
    const uint32_t ram_bytes = 0x400000u;
    board_ram = calloc(1, ram_bytes);
    board = calloc(1, sizeof *board);
    /* The same epoch the boot path uses, so a probe that reads the calendar
     * gets a stated time rather than whatever the host clock says. */
    static const ap_mc146818_time_t epoch = {
        .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
        .hour = 21u, .minute = 9u, .second = 21u,
    };
    if (entry_model == nullptr || board_ram == nullptr || board == nullptr ||
        !ap_board_init(board, board_ram, ram_bytes, &epoch, 0x012345u)) {
      free(board);
      free(board_ram);
      fprintf(stderr, "%s: cannot build the core board\n", program_name);
      return 2;
    }

    ap_machine_init_model(&board_machine, board_ram, ram_bytes, model);
    ap_machine_set_board(&board_machine, board);

    /* Through the *board*, not `ap_machine_write`: that is the operator's view
     * of flat RAM and knows nothing of where a model puts its memory. A
     * DN3500's RAM begins at `01000000`, so a board probe loads where the
     * oracle's does — which is the point, since both sides then run the same
     * addresses and the diff stops needing a base offset. */
    for (unsigned i = 0; i < word_count; i++) {
      bool hi = false, lo = false;
      ap_board_write(board, load + i * 2u, (uint8_t)(words[i] >> 8), &hi);
      ap_board_write(board, load + i * 2u + 1u, (uint8_t)words[i], &lo);
      if (!hi || !lo) {
        free(board);
        free(board_ram);
        fprintf(stderr, "%s: probe does not fit the board's RAM at %08X\n",
                program_name, (unsigned)load);
        return 2;
      }
    }
    ap_machine_reset(&board_machine, entry, stack);

    const ap_machine_run_t run = ap_machine_run(&board_machine, limit);
    result = (ap_probe_result_t){
        .executed = run.executed,
        .status = run.status,
        .d0 = board_machine.cpu.regs.d[0],
        .pc = board_machine.cpu.regs.pc,
        .bus_errors = board_machine.bus_errors,
        .hash = ap_machine_hash(&board_machine),
    };
  }

  fprintf(out, "# apollo probe file result\n");
  fprintf(out, "words     %u\n", word_count);
  fprintf(out, "ran       %u\n", result.executed);
  fprintf(out, "status    %s\n", ap_probe_status_name(result.status));
  fprintf(out, "d0        %08X\n", (unsigned)result.d0);
  fprintf(out, "pc        %08X\n", (unsigned)result.pc);
  fprintf(out, "berr      %u\n", result.bus_errors);

  if (on_board && dump_spec != NULL) {
    /* The same dump the boot path offers, on the board a probe built. Available
     * here because `board 1` makes a whole machine **without firmware**, which
     * is what lets the flag be exercised where `roms/` is absent -- and CI is
     * exactly that place. */
    uint32_t at = 0, length = 0;
    if (!parse_dump_spec(dump_spec, &at, &length)) {
      fprintf(stderr, "%s: --dump-mem wants ADDR or ADDR:LEN in hex, not %s\n",
              program_name, dump_spec);
    } else {
      fprintf(out, "memory %08X, %u byte(s), through the board\n", at, length);
      dump_memory(out, board, at, length);
    }
  }

  if (have_read && on_board) {
    uint32_t stored = 0;
    bool all = true;
    for (unsigned k = 0; k < 4u; k++) {
      bool ok = false;
      const uint8_t byte = ap_board_read(board, read_at + k, &ok);
      all = all && ok;
      stored = (stored << 8) | byte;
    }
    if (!all) {
      free(board);
      free(board_ram);
      fprintf(stderr, "%s: read address %08X is not memory on this board\n",
              program_name, (unsigned)read_at);
      return 2;
    }
    fprintf(out, "read      %08X %08X\n", (unsigned)read_at, (unsigned)stored);
    free(board);
    free(board_ram);
    return 0;
  }
  if (on_board) {
    free(board);
    free(board_ram);
    return 0;
  }

  if (have_read) {
    if ((uint64_t)read_at + 4u > (uint64_t)PROBE_RAM_BYTES) {
      fprintf(stderr, "%s: read address %08X is outside the probe RAM\n",
              program_name, (unsigned)read_at);
      return 2;
    }
    /* Big-endian, as the part stores it. */
    const uint32_t stored = (uint32_t)((uint32_t)probe_ram[read_at] << 24 |
                                       (uint32_t)probe_ram[read_at + 1u] << 16 |
                                       (uint32_t)probe_ram[read_at + 2u] << 8 |
                                       (uint32_t)probe_ram[read_at + 3u]);
    fprintf(out, "read      %08X %08X\n", (unsigned)read_at, (unsigned)stored);
  }
  return 0;
}


/* Read a whole file into memory. The frontend does this because `src/core` has
 * no file I/O by design. */
static uint8_t *read_file(const char *path, long *size_out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    return NULL;
  }
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *size_out = size;
  return bytes;
}

/* The node ID a machine should present, taken from the volume it boots.
 *
 * `board/ap_nodeid.h` takes its identifier from a caller because "a device whose
 * purpose is to be unique per machine must not be identical on every one", and
 * this is the source that caller was always meant to have. A Domain volume
 * records the node that initialised it, and a machine booting that volume has to
 * present the same one: the file system's object identifiers carry it, so a node
 * that disagreed with its own disk would create objects attributed to a machine
 * that is not there.
 *
 * Refuses rather than defaults when the file is not a volume. A node ID invented
 * from an arbitrary file configures a machine to lie about its identity, and
 * every object it then creates carries the lie -- which outlives the run and
 * cannot be traced back to the moment it was chosen. */
static bool node_id_from_volume(const char *path, uint32_t *out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open %s\n", path);
    return false;
  }
  uint8_t blocks[AP_VOLUME_LABEL_BYTES];
  const size_t got = fread(blocks, 1u, sizeof blocks, file);
  fclose(file);

  ap_volume_label_t label;
  if (!ap_volume_read_label(blocks, got, &label)) {
    fprintf(stderr, "apollo: %s is not a Domain volume\n", path);
    return false;
  }
  printf("volume %s\n", path);
  printf("  name         %s\n", label.name);
  printf("  creator UID  %08X%08X\n", label.creator.high, label.creator.low);
  printf("  node ID      %05X\n", label.node_id);
  *out = label.node_id;
  return true;
}

/* The machine's clock is not set here, and there is nothing to set it with.
 * `ap_machine_init` reads the rate from the model row, so a run through
 * `board/ap_board.c` -- the DN3500's core board -- keeps time at the DN3500's
 * rate because the machine is a DN3500, not because this file looked the model
 * up by name and said so. That is `CLAUDE.md`'s rule made true of the frontend
 * as well as the core, and it means the clock follows the table when the board
 * becomes model-driven rather than needing to be found and edited here. */

/* The whole machine as one number, with the numbers that localise a
 * disagreement beside it.
 *
 * A hash answers "are these two runs the same" and nothing else: when it says
 * no, it says nothing about where they parted. The clock and the PC are what
 * turn that into a place to look, which is why `ap_machine_state` reports them
 * together and why every run here prints the block rather than the hash alone.
 *
 * The bus-error count is here rather than in the hash deliberately -- it is our
 * record of watching the machine, not state the machine has. `ap_machine.h` has
 * the reasoning; this is the "reported beside it" half of it. */
static void report_state(const ap_machine_t *machine) {
  const ap_machine_state_t state = ap_machine_state(machine);
  printf("  state hash   %016llX\n", (unsigned long long)state.hash);
  printf("  final PC     %08X (%s)\n", state.pc,
         machine->board != NULL
             ? ap_board_region_name(ap_board_region(machine->board, state.pc))
             : "no board");
  printf("  clocks       %llu\n", (unsigned long long)state.clocks);
  /* In AP_TIME_BASE_HZ units, never CPU cycles: several nodes of different
   * models share one ring, so no CPU's cycle is a legal unit of account. A
   * machine whose clock rate was never set has produced no time at all, which
   * is visibly wrong here rather than quietly approximate. */
  printf("  elapsed      %llu base units\n", (unsigned long long)state.now);
  {
    /* Exceptions by vector, which is the machine's own account of what went
     * wrong: the firmware prints at most the one it stopped on, and a handler
     * that reports one exception as another is invisible without this. */
    bool any = false;
    for (unsigned v = 0; v < 256u; v++) {
      if (machine->cpu.exceptions_taken[v] == 0u) {
        continue;
      }
      if (!any) {
        printf("  exceptions  ");
        any = true;
      }
      printf(" %u x vector %u", machine->cpu.exceptions_taken[v], v);
    }
    if (any) {
      printf("\n");
    }
  }
  {
    /* Whether translation is on, and the two windows that bypass it.
     *
     * A descriptor-fetch count says the MMU has been *used*; it does not say
     * whether a given access went through it, because a transparent window is
     * consulted first and answers without a table search. A run that reports
     * translation enabled and a window covering all of memory is describing a
     * machine whose page tables cannot matter, and that is not visible from any
     * other number here. */
    const ap_m68030_cpu_t *cpu = &machine->cpu;
    printf("  translation  %s", cpu->tc.enable ? "enabled" : "off");
    for (unsigned t = 0; t < 2u; t++) {
      const ap_m68030_tt_t *tt = t == 0u ? &cpu->tt0 : &cpu->tt1;
      if (!tt->enabled) {
        continue;
      }
      printf(", tt%u base %02X mask %02X", t, tt->logical_base,
             tt->logical_mask);
    }
    if (cpu->tc.enable) {
      /* Which root a supervisor access uses is `TC`'s SRE, and the two roots
       * are loaded by separate `PMOVE`s -- so a program that loads one and not
       * the other translates through a table that was never filled in. */
      printf(", %s root", cpu->tc.supervisor_root ? "split supervisor" :
                                                    "one");
      printf(", crp %08X limit %04X, srp %08X limit %04X",
             cpu->crp.table_address, cpu->crp.limit, cpu->srp.table_address,
             cpu->srp.limit);
    }
    printf("\n");
  }
  printf("  atc fills    %u descriptor fetch(es), %u history update(s)\n",
         state.table_fetches, state.table_updates);
  if (machine->distinct_fault_count > 0u) {
    printf("  fault sites ");
    for (unsigned i = 0; i < machine->distinct_fault_count; i++) {
      printf(" %08X", machine->distinct_faults[i]);
    }
    printf("\n");
  }
  printf("  bus errors   %u", state.bus_errors);
  if (state.bus_errors > 0u) {
    printf(", first %08X, last %08X from PC %08X", state.first_bus_error,
           state.last_bus_error, state.last_bus_error_pc);
  }
  printf("\n");
}

/* Run the machine from its boot PROM, which is how a DN3500 actually starts.
 *
 * The side-loading route (`--boot-tape`) puts an image at an address it names
 * and jumps there. That works only while memory answers everywhere: the real
 * machine has no physical memory at the boot image's load address, and the
 * addresses in its header are logical (`FINDINGS.md` C28). The PROM is what
 * enables translation, so it is what has to run first. */
/* Scan the fitted screen out and write it as a PNG.
 *
 * The plan's verification line for the drawing engine asks for exactly this and
 * for nothing weaker: "verify on a decoded PNG rather than on register
 * round-trips — a controller that passes register tests and draws nothing is
 * the standard way this goes wrong". A mirrored, sheared or blank image passes
 * every register identity in `graphics_suite`.
 *
 * ## The palette, and what this cannot yet claim
 *
 * A monochrome screen has a real one: the bitmap stores **ink**, so a set bit
 * is black and a clear bit white. That is the whole colour model and it is
 * exact.
 *
 * A colour screen does not, here. The 8-plane board's lookup table is a Bt458
 * and `ap_bt458` models it completely — but it is **not wired to the board**,
 * so an index cannot yet become a colour; the 4-plane board's own 16-entry
 * table is not modelled at all. Rather than invent colours, a colour screenshot
 * is written as an *index map* under an even grey ramp, and the console says
 * so. It still catches every geometric failure, which is what the verification
 * is for; it does not claim to be what the monitor showed. */
static int write_screenshot(const char *path, const ap_graphics_t *graphics,
                            uint8_t cr1) {
  ap_graphics_geometry_t geometry;
  if (!ap_graphics_geometry(graphics->screen, &geometry)) {
    fprintf(stderr, "apollo: no screen fitted, nothing to capture\n");
    return 1;
  }
  if (!ap_png_available()) {
    fprintf(stderr, "apollo: %s\n", ap_png_status_name(AP_PNG_UNSUPPORTED));
    return 1;
  }

  const uint32_t pixels = (uint32_t)geometry.width * geometry.height;
  uint8_t *image = calloc(1, pixels);
  if (image == NULL) {
    fprintf(stderr, "apollo: cannot allocate the screenshot\n");
    return 1;
  }
  if (ap_graphics_scanout(graphics, cr1, image, pixels) != pixels) {
    free(image);
    fprintf(stderr, "apollo: the screen could not be scanned out\n");
    return 1;
  }

  const unsigned colours = 1u << geometry.planes;
  uint8_t palette[256][3];
  bool real_palette = false;
  if (geometry.planes == 1u) {
    /* Ink, not light: a set bit is black. That is the whole colour model for a
     * monochrome screen and it is exact. */
    palette[0][0] = palette[0][1] = palette[0][2] = 0xFFu;
    palette[1][0] = palette[1][1] = palette[1][2] = 0x00u;
    real_palette = true;
  } else if (graphics->screen == AP_SCREEN_COLOUR_8_PLANE) {
    /* The Bt458's own, as the firmware loaded it. */
    real_palette = true;
    for (unsigned i = 0; i < colours; i++) {
      uint8_t rgb[3] = {0u, 0u, 0u};
      (void)ap_bt458_palette(&graphics->lut, i, rgb);
      palette[i][0] = rgb[0];
      palette[i][1] = rgb[1];
      palette[i][2] = rgb[2];
    }
  } else {
    /* A 4-plane board's lookup table is sixteen entries written through three
     * registers of the controller's own, and is not modelled. An even ramp,
     * labelled as an index map rather than passed off as colours. */
    for (unsigned i = 0; i < colours; i++) {
      const uint8_t level = (uint8_t)(i * 255u / (colours - 1u));
      palette[i][0] = palette[i][1] = palette[i][2] = level;
    }
  }

  const ap_png_status_t status =
      ap_png_write_indexed(path, image, geometry.width, geometry.height,
                           palette, colours);
  free(image);
  if (status != AP_PNG_OK) {
    fprintf(stderr, "apollo: cannot write %s: %s\n", path,
            ap_png_status_name(status));
    return 1;
  }

  printf("  screenshot   %s, %ux%u, %u plane(s)\n", path, geometry.width,
         geometry.height, geometry.planes);
  if (!ap_graphics_display_enabled(cr1)) {
    /* Reported rather than painted black, so that "the firmware never enabled
     * the display" and "the firmware drew nothing" stay different answers. */
    printf("               DISP_EN is clear: the monitor would show black\n");
  }
  if (!real_palette) {
    printf("               index map under a grey ramp, not the screen's"
           " colours -- a\n"
           "               4-plane board's lookup table is not modelled\n");
  } else if (geometry.planes > 1u) {
    printf("               palette is the Bt458's, as the firmware loaded"
           " it\n");
  }
  return 0;
}

/* One step, kept rather than printed.
 *
 * `--boot-trace` prints every step, which answers "what did the machine do" for
 * a run of a few thousand instructions and nothing at all for a run of five
 * hundred million: the fault this was built for is half a billion steps in, and
 * the output would be terabytes of a file nobody can open.
 *
 * `--boot-trace-last N` keeps the last N steps in a ring and prints them when
 * the run ends. The registers are the ones a failure is read from -- `d0` and
 * `d1` because the PROM's own reporter prints them as "Expected" and "Actual",
 * `a0` because it prints that as "Address", and `a6`/`a7` because the firmware
 * bases its data on one and its calls on the other. */
typedef struct {
  unsigned step;
  uint32_t pc;
  uint32_t a7;
  uint32_t a6;
  uint32_t a0;
  uint32_t d0;
  uint32_t d1;
  uint16_t instruction;
  ap_m68030_step_status_t status;
} ap_trace_ring_t;

static int boot_from_prom(const char *path, unsigned limit, bool trace,
                          uint32_t watch, const char *input, unsigned input_unit,
                          unsigned input_channel, uint8_t input_rate,
                          unsigned input_interval_us,
                          unsigned key, bool console,
                          ap_screen_kind_t screen, uint32_t node_id,
                          ap_model_id_t model, const char *screenshot,
                          unsigned trace_last, uint32_t stop_pc,
                          const char *disk_path, const char *dump_spec) {
  long size = 0;
  uint8_t *prom = read_file(path, &size);
  if (prom == NULL) {
    fprintf(stderr, "apollo: cannot read boot PROM %s\n", path);
    return 1;
  }

  /* 16 MB, which is a size the RAM configuration table covers -- the boot PROM
   * reads that strap to size memory and a machine whose size is not in the
   * table cannot tell it anything. It was 4 MB, which is not a configuration a
   * DN3500 can be built in: four banks of 4 MB is the smallest the byte
   * describes at all. */
  uint32_t ram_bytes = 16u * 1024u * 1024u;
  uint8_t *ram = calloc(1, ram_bytes);
  ap_board_t *board = calloc(1, sizeof *board);
  static const ap_mc146818_time_t epoch = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  /* The memory array's parity RAM: one bit per byte, allocated here because the
   * core allocates nothing. A real board has it, so a booted machine gets it --
   * self-test 7 forces bad parity and expects the level 7 interrupt back. */
  const uint32_t parity_bytes = (ram_bytes + 7u) / 8u;
  uint8_t *parity = calloc(1, parity_bytes);
  ap_trace_ring_t *trace_ring =
      trace_last > 0u ? calloc(trace_last, sizeof *trace_ring) : NULL;
  unsigned trace_ring_used = 0;
  if (trace_last > 0u && trace_ring == NULL) {
    fprintf(stderr, "apollo: cannot keep %u trace step(s)\n", trace_last);
    return 1;
  }
  if (ram == NULL || board == NULL || parity == NULL ||
      !ap_board_init_model(board, ram, ram_bytes, &epoch, node_id, model) ||
      !ap_board_attach_parity(board, parity, parity_bytes)) {
    free(trace_ring);
    free(trace_ring);
  free(parity);
    free(board);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: cannot build the core board\n");
    return 1;
  }
  /* Fit a display, if one was asked for. The memories are allocated here rather
   * than in the core, which allocates nothing -- and only when a screen is
   * fitted, so a machine without one has no frame buffer rather than an empty
   * one. */
  uint8_t *colour_memory = NULL;
  uint8_t *mono_memory = NULL;
  if (screen != AP_SCREEN_NONE) {
    /* The card's whole image memory, which is **not** the size of the CPU's
     * window onto it. An 8-plane board carries 1 MB in eight planes and the
     * window at `0A0000-0BFFFF` is 128 KB -- one plane -- which is why the
     * plane-select registers exist at all. Allocating the window's size, which
     * is what this did first, gives a card with one eighth of its memory and a
     * scanout that correctly refuses to run.
     *
     * The window is **exactly one plane** -- 128 KB is 1024x1024 bits, and the
     * 1280x1024 monochrome board's 256 KB is 2048x1024 -- so an offset in it is
     * a word offset *within* a plane and there is no plane selector to model.
     * Which planes an access reaches is `CR2`'s, applied by the blitter's plane
     * loop. This carried a documented approximation until the two sizes were
     * put beside each other; the arithmetic is the proof. */
    ap_graphics_geometry_t geometry;
    const bool fitted = ap_graphics_geometry(screen, &geometry);
    const uint32_t image_bytes =
        fitted ? geometry.plane_words * 2u * geometry.planes : 0u;
    const uint32_t window_colour =
        AP_GRAPHICS_COLOUR_MEMORY_END - AP_GRAPHICS_COLOUR_MEMORY_ADDR + 1u;
    const uint32_t window_mono =
        AP_GRAPHICS_MONO_MEMORY_END - AP_GRAPHICS_MONO_MEMORY_ADDR + 1u;
    /* Whichever is larger: the memory must hold the picture, and the window
     * must not address past the end of the buffer it decodes into. */
    const bool is_colour = ap_graphics_is_colour(screen);
    const uint32_t colour_bytes =
        (is_colour && image_bytes > window_colour) ? image_bytes : window_colour;
    const uint32_t mono_bytes =
        (!is_colour && image_bytes > window_mono) ? image_bytes : window_mono;
    colour_memory = calloc(1, colour_bytes);
    mono_memory = calloc(1, mono_bytes);
    if (colour_memory == NULL || mono_memory == NULL) {
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
    free(trace_ring);
  free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: cannot allocate the graphics memories\n");
      return 1;
    }
    ap_graphics_init(&board->graphics, screen);
    ap_graphics_attach_memory(&board->graphics, colour_memory, colour_bytes,
                              mono_memory, mono_bytes);
  }

  /* Fit a Winchester, if one was named. The controller and the image reader
   * have both been complete for a while and nothing ever handed one to the
   * other, so every boot so far has run on a machine with **no disk**: the
   * firmware finds no drive, which is a different failure from finding a broken
   * one and was being read as the latter.
   *
   * The image is owned here, as every image in this core is, and stays mapped
   * for the whole run. 348 MB is the reference drive; a shorter file is opened
   * against the same geometry and reads past its end fail, which is what a
   * partly written image should do. */
  uint8_t *disk_bytes = NULL;
  ap_awd_t disk_image;
  if (disk_path != NULL) {
    long disk_size = 0;
    disk_bytes = read_file(disk_path, &disk_size);
    if (disk_bytes == NULL) {
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
    free(trace_ring);
  free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: cannot read disk image %s\n", disk_path);
      return 1;
    }
    if (!ap_awd_open(&disk_image, disk_bytes, (size_t)disk_size,
                     ap_awd_geometry_for(AP_AWD_DRIVE_348MB), false)) {
      free(disk_bytes);
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(trace_ring);
    free(trace_ring);
  free(parity);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: %s is not an Apollo Winchester image\n",
              disk_path);
      return 1;
    }
    ap_omti_attach(&board->disk.controller, &disk_image);
    printf("disk %s, %ld bytes\n", disk_path, disk_size);
  }

  if (!ap_board_load_prom(board, prom, (uint32_t)size)) {
    free(board);
    free(trace_ring);
    free(trace_ring);
  free(parity);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: %s does not fit the boot PROM region\n", path);
    return 1;
  }

  uint32_t stack = 0;
  uint32_t pc = 0;
  if (!ap_board_reset_vector(board, &stack, &pc)) {
    free(board);
    free(trace_ring);
    free(trace_ring);
  free(parity);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: %s carries no reset vector\n", path);
    return 1;
  }

  /* A watch address must name memory. This reads through `ap_board_read` every
   * step, so watching a device register would *perturb the machine*: reading a
   * DUART's receive buffer pops its FIFO, and every read inflates the
   * per-region counters. An instrument that changes what it measures is worse
   * than none, and a comment saying so is not a guard -- the mistake is one
   * keystroke away and its symptom is a run that is merely *different*, with
   * nothing to say it was the watching that changed it. */
  if (watch != 0u) {
    const ap_board_region_t region = ap_board_region(board, watch);
    if (region != AP_BOARD_REGION_RAM && region != AP_BOARD_REGION_PROM) {
      fprintf(stderr,
              "apollo: --boot-watch %08X is in %s, not memory. Watching a "
              "device register would change the run it is measuring.\n",
              watch, ap_board_region_name(region));
      free(board);
      free(trace_ring);
    free(trace_ring);
  free(parity);
      free(ram);
      free(prom);
      return 1;
    }
  }

  printf("boot PROM %s\n", path);
  printf("  size         %lu\n", (unsigned long)size);
  printf("  reset SSP    %08X (%s)\n", stack,
         ap_board_region_name(ap_board_region(board, stack)));
  printf("  reset PC     %08X (%s)\n", pc,
         ap_board_region_name(ap_board_region(board, pc)));

  ap_machine_t machine;
  /* The same model as the board, or the processor would run at one machine's
   * clock over another machine's address space. */
  ap_machine_init_model(&machine, ram, ram_bytes, model);
  ap_machine_set_board(&machine, board);
  ap_machine_reset(&machine, pc, stack);

  /* Scripted serial input, delivered a byte at a time as the firmware takes the
   * previous one. Not host input: a byte sequence decided before the run
   * starts, so the run stays reproducible. That is the whole reason this
   * frontend exists, and a getchar() here would end it.
   *
   * The port *and channel* are choices, not constants. The oracle settles
   * which is which: the keyboard drives serial 1 channel A and a terminal
   * drives serial 1 channel B, so ASCII belongs on B and scan codes on A.
   *
   * The port is a choice, not a constant: the PROM's poll loop tests *both*
   * DUARTs and branches differently for each, so which one carries the console
   * is a question the firmware answers rather than one to assume. */
  unsigned key_state = 0u; /* 0 press, 1 release, 2 done */
  size_t input_sent = 0;
  const size_t input_length = input != NULL ? strlen(input) : 0u;

  /* One character time at the line's own rate, which is the floor the *wire*
   * imposes: a character cannot be delivered closer together than its own
   * framing takes, whatever the far end is doing.
   *
   * **Ten bit times was the assumption here, and it is only right for 8N1.**
   * The frame is a start bit, the data bits `MR1[1:0]` asks for, a parity bit
   * when `MR1[2]` is clear, and a stop length from `MR2[3:0]` -- which is
   * sixteen encodings from 0.5 to 2 bits, not a one-or-two flag. So the figure
   * comes from the mode registers now, through
   * `ap_mc68681_character_time`, and a link running with parity or a long stop
   * is paced at the rate it actually runs at rather than a tenth faster.
   *
   * `MD.md`'s capture used 0.4 s between carriage returns, which is a person
   * typing and is four hundred times this. The requirement it was recording is
   * satisfied by any gap long enough for the firmware to rewrite a clock select
   * between characters; the physical floor is the one this can justify without
   * choosing a number, and it is three orders of magnitude cheaper to run.
   *
   * Zero for the rates that are not a fixed rate -- the timer and the two
   * external clocks -- where this cannot know the character time and so imposes
   * no gap. */
  const unsigned input_baud =
      ap_mc68681_baud((uint8_t)(input_rate & 0x0Fu), true);
  /* How long to leave between scripted characters.
   *
   * The floor is one character time -- ten bit times at the line's own rate --
   * because a byte cannot be delivered faster than the wire carries it, and
   * this defaulted to exactly that. `docs/references/MD.md` records a different
   * figure from the other side: "one carriage return every 0.4 s on standard
   * input, not a pipe delivered at once". Those are not the same requirement
   * and the code claimed the second while implementing the first, which is 400
   * times faster at 9600 baud.
   *
   * So the interval is settable, and the wire's floor is enforced under
   * whatever is asked for -- a caller cannot request something the line cannot
   * carry. `input_interval_us` of zero keeps the floor, which is what every
   * existing use expects. */
  const ap_time_t wire_floor = ap_sio_character_time(
      &machine.board->sio, input_unit, input_channel, input_baud);
  ap_time_t requested = 0u;
  if (input_interval_us > 0u) {
    requested = (ap_time_t)input_interval_us * (AP_TIME_BASE_HZ / 1000000u);
  }
  const ap_time_t input_interval =
      requested > wire_floor ? requested : wire_floor;
  ap_time_t input_next_at = 0u;

  ap_machine_run_t run;
  if (trace || trace_last > 0u || input_length > 0u || console ||
      key < AP_KBD_KEYS) {
    /* Step by step, reporting the program counter and the active stack pointer.
     *
     * A7 is the observable this exists for. A wrong PC is where damage becomes
     * visible; a stack pointer that stops matching the call depth is where it
     * happens, and the two can be thousands of instructions apart. Printing
     * both together is what lets one be found from the other. */
    run = (ap_machine_run_t){.status = AP_M68030_STEP_EXECUTED};
    if (trace && trace_last == 0u) {
      printf("# step pc a7 a6 a0 instruction status%s\n",
             watch != 0u ? " watched" : "");
    }
    for (unsigned i = 0; i < limit; i++) {
      /* Feed the next byte only once the program has taken the last **and** a
       * terminal's worth of time has passed since the one before.
       *
       * "The program has taken the last" alone is not a terminal, it is a pipe:
       * it delivers the next character microseconds later, and the firmware's
       * console negotiation needs the gap. Measured, the two are 35 instructions
       * apart -- about 5.6 us against a real terminal's 0.4 s, some seventy
       * thousand times too fast.
       *
       * What that costs is not a lost byte but a *wrong* one. The autobaud arms
       * itself on a mis-framed character and reprograms the port; the character
       * that follows is meant to arrive at the new rate and be the clean one.
       * Delivered before the firmware has rewritten the clock select, it is the
       * old rate's garbage instead, it consumes the armed state, and the clean
       * character that comes next has nothing left to consume it. The
       * negotiation cycles forever making progress it immediately loses.
       *
       * `docs/references/MD.md` recorded the requirement from the other side --
       * "one carriage return every 0.4 s on standard input, not a pipe
       * delivered at once" -- and this could not honour it until the machine
       * advanced time at all, which is a Phase 3 tick-loop item away. */
      if (input_sent < input_length &&
          ap_machine_now(&machine) >= input_next_at &&
          !ap_sio_receiver_ready(&board->sio, input_unit, input_channel) &&
          ap_sio_character_bits(&board->sio, input_unit, input_channel) == 8u &&
          ap_sio_receiver_enabled(&board->sio, input_unit, input_channel)) {
        /* The same three conditions `--boot-key` needs, and for the same
         * reason: `MR1` resets to a five-bit link and a disabled receiver drops
         * what arrives, so a script that sent as soon as the FIFO was free was
         * sending into a port that could neither carry nor keep the byte.
         *
         * It matters more here than it looks. The firmware's autobaud
         * identifies the sender's rate from *what the wrong rate did to the
         * character*, so a byte truncated to five bits first arrives as a shape
         * the autobaud has no case for -- and the negotiation cannot even
         * begin. */
        /* Sent at the rate the terminal is set to. `77` is what the DN3500's
         * own firmware configures both ports to at reset, measured off the
         * oracle -- so a scripted terminal that used anything else would be
         * modelling a misconfigured cable rather than a console. */
        ap_sio_receive_at(&board->sio, input_unit, input_channel,
                          (uint8_t)input[input_sent], input_rate);
        input_next_at = ap_machine_now(&machine) + input_interval;
        /* Advance only if the receiver actually took it. A DUART whose receiver
         * is still disabled drops the byte, and the firmware enables it long
         * after reset -- so a script that advanced regardless would deliver its
         * whole text into a switched-off port and then wait forever for the
         * first character. Retrying costs nothing and cannot lose a byte. */
        if (ap_sio_receiver_ready(&board->sio, input_unit, input_channel)) {
          input_sent++;
        }
      }
      /* Drain both ports' transmitters every step, so nothing is lost when the
       * firmware writes two characters before we look again. Written straight
       * to stdout: this is the machine's console, and the whole point is to see
       * what it says rather than to interpret it. */
      for (unsigned unit = 0; unit < 2u; unit++) {
        for (unsigned channel = 0; channel < 2u; channel++) {
          uint8_t out_byte = 0;
          while (ap_sio_transmit(&board->sio, unit, channel, &out_byte)) {
            fputc((int)out_byte, stdout);
          }
        }
      }
      /* Press the key once the port can take it, then release on the next
       * opportunity -- the same self-timing as scripted input, and for the same
       * reason: a fixed step number would be a guess about how long the
       * firmware takes to enable its receiver, and would silently do nothing if
       * it took longer. */
      if (key < AP_KBD_KEYS && key_state < 2u &&
          !ap_sio_receiver_ready(&board->sio, 0u, 0u) &&
          ap_sio_character_bits(&board->sio, 0u, 0u) == 8u &&
          ap_sio_receiver_enabled(&board->sio, 0u, 0u)) {
        /* Eight bits, not merely a free receiver.
         *
         * `MR1` resets to a **five-bit** link, and this sent as soon as the
         * receiver was free -- which is immediately, thousands of instructions
         * before the firmware programs the port. A make code arrived with its
         * top three bits missing and a release code, which is the make code with
         * bit 7 set, could not arrive at all: both became `00`. So every
         * `--boot-key` run this project has taken delivered nothing, and looked
         * exactly like a run where the firmware ignored the keyboard.
         *
         * And enabled as well as eight bits wide: a disabled receiver drops
         * what arrives, so waiting only for the width delivered into a port
         * that was not yet listening -- measured, the byte went in at step 1223
         * and the FIFO stayed empty.
         *
         * The port's own configuration is the condition to wait on, which is
         * what "self-timed" was always supposed to mean here. */
        const bool moved = (key_state == 0u) ? ap_board_key_press(board, key)
                                             : ap_board_key_release(board, key);
        if (moved) {
          key_state++;
        }
      }
      const uint32_t step_pc = machine.cpu.regs.pc;
      /* One instruction through the *machine*, not through the processor.
       *
       * This called `ap_m68030_step` directly, which is the CPU and nothing
       * else: no interrupt sampling, no bus tick, no stall for another master,
       * and no device advanced. So the boot -- the most important path this
       * frontend has -- ran on a machine where no time passed at all, and every
       * timer the firmware programmed stood still while it waited for one. The
       * `elapsed` line reported zero for exactly that reason and nobody read
       * it as the symptom it was.
       *
       * `ap_machine_run` with a limit of one is the whole of the machine's own
       * loop, once. Keeping the frontend's stepping on that rather than beside
       * it is what stops the two diverging again. */
      const ap_machine_run_t one = ap_machine_run(&machine, 1u);
      /* The instruction word is read back from where it executed, since the
       * machine's loop reports why a run ended and not which word did it. */
      uint32_t executed_word = 0;
      (void)ap_machine_read(&machine, step_pc, 2u, &executed_word);
      const ap_m68030_step_result_t r = {
          .status = one.status, .instruction = (uint16_t)executed_word};
      /* A6 as well as A7: the firmware uses it as a base pointer for its own
       * data, and whether those two overlap is the question a trace has to be
       * able to answer. */
      if (stop_pc != 0u && step_pc == stop_pc) {
        /* Checked **before** the fast path below, not after it.
         *
         * It was after, so the stop only happened when a trace or a ring was
         * also asked for -- and a run without one reported nothing and looked
         * exactly like a run whose address was never reached. That is worse
         * than not having the flag: it answers a question it did not test, and
         * I read two such runs as evidence that an address was never executed.
         *
         * The run ends here so a ring kept alongside it holds the steps that
         * led to this rather than the ones that came after. */
        printf("  stopped at   PC %08X after %u instruction(s)\n", stop_pc, i);
        run.executed++;
        break;
      }
      if (!trace && trace_last == 0u) {
        run.status = r.status;
        if (r.status != AP_M68030_STEP_EXECUTED &&
            r.status != AP_M68030_STEP_EXCEPTION) {
          break;
        }
        run.executed++;
        continue;
      }
      if (trace_last > 0u) {
        /* Kept, not printed. See `trace_last`'s declaration: a fault half a
         * billion instructions in cannot be reached by printing every step. */
        ap_trace_ring_t *slot = &trace_ring[trace_ring_used % trace_last];
        slot->step = i;
        slot->pc = step_pc;
        slot->a7 = ap_m68030_read_a7(&machine.cpu.regs);
        slot->a6 = machine.cpu.regs.a[6];
        slot->a0 = machine.cpu.regs.a[0];
        slot->d0 = machine.cpu.regs.d[0];
        slot->d1 = machine.cpu.regs.d[1];
        slot->instruction = r.instruction;
        slot->status = r.status;
        trace_ring_used++;
        run.status = r.status;
        if (r.status != AP_M68030_STEP_EXECUTED &&
            r.status != AP_M68030_STEP_EXCEPTION) {
          break;
        }
        run.executed++;
        continue;
      }
      printf("%u %08X %08X %08X %08X %04X %s\n", i, step_pc,
             ap_m68030_read_a7(&machine.cpu.regs), machine.cpu.regs.a[6],
             machine.cpu.regs.a[0], r.instruction,
             ap_probe_status_name(r.status));
      if (watch != 0u) {
        /* One named location's contents, per step. Three hypotheses about this
         * value were each refuted by an observable rather than by argument, so
         * the cheapest thing left is to stop reasoning about what writes it and
         * simply watch it change.
         *
         * **Memory only.** This reads through `ap_board_read`, so pointing it
         * at a device register would *perturb the machine*: reading a DUART's
         * receive buffer pops its FIFO, and every read here inflates the
         * per-region counters below. An instrument that changes what it
         * measures is worse than none, and this one gives no warning -- the run
         * simply becomes a different run. */
        uint32_t held = 0;
        bool all = true;
        for (unsigned k = 0; k < 4u; k++) {
          bool byte_ok = false;
          const uint8_t byte = ap_board_read(board, watch + k, &byte_ok);
          all = all && byte_ok;
          held = (held << 8) | byte;
        }
        printf("  watch %08X = %08X%s\n", watch, held, all ? "" : " (unmapped)");
      }
      run.status = r.status;
      if (r.status != AP_M68030_STEP_EXECUTED &&
          r.status != AP_M68030_STEP_EXCEPTION) {
        break;
      }
      run.executed++;
    }
  } else {
    run = ap_machine_run(&machine, limit);
  }
  if (trace_last > 0u && trace_ring_used > 0u) {
    /* Oldest first, so it reads forwards like the run did. A ring that has not
     * filled yet starts at zero; one that has starts at the next slot to be
     * overwritten. */
    const unsigned kept =
        trace_ring_used < trace_last ? trace_ring_used : trace_last;
    const unsigned first = trace_ring_used < trace_last
                               ? 0u
                               : trace_ring_used % trace_last;
    printf("# last %u step(s): step pc a7 a6 a0 d0 d1 instruction status\n",
           kept);
    for (unsigned k = 0; k < kept; k++) {
      const ap_trace_ring_t *e = &trace_ring[(first + k) % trace_last];
      printf("%u %08X %08X %08X %08X %08X %08X %04X %s\n", e->step, e->pc,
             e->a7, e->a6, e->a0, e->d0, e->d1, e->instruction,
             ap_probe_status_name(e->status));
    }
  }
  printf("  executed     %u instruction(s)\n", run.executed);
  printf("  stopped      %s\n", ap_probe_status_name(run.status));
  report_state(&machine);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_write)));
  }
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  if (board->rom_writes > 0u) {
    printf("    first      %08X\n", board->first_rom_write);
  }
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  if (board->atbus_empty_reads > 0u) {
    printf("    first read %08X\n", board->first_atbus_empty_read);
  }
  if (board->atbus_empty_writes > 0u) {
    printf("    first write %08X\n", board->first_atbus_empty_write);
  }
  /* The two registers Table 2-8 names and this core declines. Printed even at
   * zero for the same reason: "the firmware never touched task alias" is an
   * answer, and the only one available until a handbook turns up. */
  printf("  declined     task alias %u/%u, master request %u/%u (read/write)\n",
         board->task_alias_reads, board->task_alias_writes,
         board->master_request_reads, board->master_request_writes);
  /* The part of the translation map's region no manual describes. Reported even
   * at zero, because zero is the informative answer here: it says the run never
   * went anywhere our assumed decode could be wrong. */
  printf("  atmap undoc  %u read, %u written\n",
         board->atmap_undescribed_reads, board->atmap_undescribed_writes);
  if (board->atmap_undescribed_reads > 0u) {
    printf("    first read %08X\n", board->first_atmap_undescribed_read);
  }
  if (board->atmap_undescribed_writes > 0u) {
    printf("    first write %08X\n", board->first_atmap_undescribed_write);
  }
  /* The machine's own account of what went wrong. A self-test failure posts a
   * diagnostic code to the control register -- the LEDs -- rather than to any
   * console, and then flashes it for ever. Discarding those values threw away
   * the one thing the firmware says about the failure. `FINDINGS.md` C109 has
   * the post routine and the codes seen so far. */
  if (board->registers.posted_count > 0u) {
    printf("  posted codes ");
    for (unsigned i = 0; i < board->registers.posted_count; i++) {
      printf("%s%02X", i == 0u ? "" : " ", board->registers.posted[i]);
    }
    printf("  (%u write(s), distinct in order, as written)\n",
           board->registers.posted_total);
  }
  /* What the machine told its firmware about its own memory. Printed even when
   * known, because a wrong configuration byte is a machine that sizes memory it
   * does not have and finds out mid-self-test. */
  if (board->ram_config_known) {
    printf("  ram config   %02X strapped on serial 1's input port\n",
           board->ram_config);
  } else {
    printf("  ram config   **not strapped**: no table entry for this model at"
           " this size,\n"
           "               so the firmware reads zero and sizes no memory at"
           " all\n");
  }
  /* Why scripted input did or did not arrive, which a serial read count cannot
   * say. Delivery is gated on the port being programmed to eight bits and its
   * receiver enabled -- `MR1` resets to a five-bit link and a disabled receiver
   * drops what arrives -- and a script blocked on either looks exactly like a
   * firmware that is ignoring the console. Printed whenever a script was given,
   * including when all of it went, because "all delivered and still silent" is
   * a different finding from "none delivered". */
  if (input_length > 0u) {
    printf("  input        %zu of %zu character(s) delivered, sent at %u baud"
           " (CSR %02X)\n",
           input_sent, input_length,
           ap_mc68681_baud((uint8_t)(input_rate >> 4), false), input_rate);
    for (unsigned unit = 0; unit < 2u; unit++) {
      for (unsigned ch = 0; ch < 2u; ch++) {
        /* The rate the *machine* is listening on, beside what the terminal
         * sent at. Those are the two numbers the console negotiation is
         * about, and until they were printed together the only way to compare
         * them was to infer one from whether a character survived. A `CR` is
         * `0D` and forgiving of being sampled at the wrong instants; a letter
         * is not -- so a mismatch here shows as carriage returns working and
         * everything else vanishing. */
        const uint8_t csr = ap_sio_clock_select(&board->sio, unit, ch);
        printf("    sio%u %c      %u bits, receiver %s, listening at %u baud"
               " (CSR %02X)\n",
               unit + 1u, ch == 0u ? 'A' : 'B',
               ap_sio_character_bits(&board->sio, unit, ch),
               ap_sio_receiver_enabled(&board->sio, unit, ch) ? "enabled"
                                                             : "disabled",
               ap_mc68681_baud((uint8_t)(csr >> 4), false), csr);
      }
    }
  }
  /* Which of the disk controller's registers a run touched. A region total
   * says the firmware talked to it and cannot say *what it asked*, and six
   * million reads against seven writes is a poll whose target is the whole
   * question. */
  if (board->disk.command_total > 0u) {
    printf("  disk commands %u issued:", board->disk.command_total);
    for (unsigned c = 0; c < 256u; c++) {
      if (board->disk.commands[c] != 0u) {
        printf(" %02X x%u", c, board->disk.commands[c]);
      }
    }
    printf("\n");
    /* And how the last one ended. A command histogram says what was asked and
     * a `03 REQUEST SENSE` beside a `08 READ` says the read failed, but neither
     * says *why the controller refused* -- which is the one thing the sense
     * bytes exist to answer and the one thing a run could not report. */
    const ap_omti_t *omti = &board->disk.controller;
    printf("  disk last     %02X, %s, sense %02X %02X %02X %02X, next lba %u\n",
           omti->last_command,
           omti->completion == 0u ? "completed" : "error",
           omti->sense[0], omti->sense[1], omti->sense[2], omti->sense[3],
           omti->next_lba);
  }
  for (unsigned r = 0; r < AP_OMTI_DISK_REGISTERS; r++) {
    if (board->disk.disk_reads[r] == 0u &&
        board->disk.disk_writes[r] == 0u) {
      continue;
    }
    printf("    disk reg %u  %8u read(s) %8u write(s)\n", r,
           board->disk.disk_reads[r],
           board->disk.disk_writes[r]);
  }
  for (unsigned r = 0; r < AP_OMTI_FLOPPY_REGISTERS; r++) {
    if (board->disk.floppy_reads[r] == 0u &&
        board->disk.floppy_writes[r] == 0u) {
      continue;
    }
    printf("    fdc reg %u   %8u read(s) %8u write(s)\n", r,
           board->disk.floppy_reads[r],
           board->disk.floppy_writes[r]);
  }
  /* The lookup table's third chip select is an **A/D converter**, and what it
   * converts is the controller's own video output -- the level on one gun at
   * wherever the beam is.
   * Reported because the firmware range-checks the answers and posts a
   * diagnostic code if either is outside `[52, 70)`. */
  if (board->graphics.lut_ad_accesses > 0u) {
    printf("  lut a/d      %u access(es) (channel selects and conversions)\n",
           board->graphics.lut_ad_accesses);
  }
  /* The display controller's *memory*, apart from its registers -- which
   * `region_writes` counts together and so cannot tell apart. "The firmware
   * never wrote a pixel" and "it wrote and nothing drew" are different answers
   * and only the second is a defect. Printed even at zero, for that reason. */
  if (board->parity.forced_writes > 0u || board->parity.errors > 0u ||
      board->parity.unstorable_writes > 0u) {
    printf("  parity       %u forced write(s), %u error(s)",
           board->parity.forced_writes, board->parity.errors);
    if (board->parity.errors > 0u) {
      printf(", first at %08X",
             board->map->ram_base + board->parity.first_error_offset);
    }
    if (board->parity.unstorable_writes > 0u) {
      /* A board asked to force bad parity with no parity RAM fitted. Said out
       * loud, because a self-test that passes on such a board passes for the
       * wrong reason. */
      printf(", %u with no parity RAM fitted",
             board->parity.unstorable_writes);
    }
    printf("\n");
  }
  for (unsigned u = 0; u < 2u; u++) {
    /* The four registers that decide whether a transfer can happen at all.
     * A software request is non-maskable and a **pin** is not -- and the
     * cascade reaches controller 2 as a pin, so a masked channel 0 there stops
     * every transfer on controller 1 without either controller looking wrong on
     * its own. That is invisible from a transfer count. */
    const ap_i8237_t *c = &board->dma.controller[u];
    /* Both, always. Skipping a controller that looks untouched hid *which* of
     * the two a program had reached, which is the question when a register
     * decode is in doubt -- and it made a report that named one controller read
     * as though the other did not exist. */
    printf("  dma%u regs    command %02X, mask %01X, request %01X, status %02X\n",
           u + 1u, c->command, c->mask & 0x0Fu, c->request & 0x0Fu, c->status);
    for (unsigned ch = 0; ch < 2u; ch++) {
      /* The two channels a memory-to-memory service uses, with both the base
       * and the current values: a current address that has advanced past its
       * base is a transfer that ran, and a base that is not what the program
       * wrote is a decode fault. Neither shows in a translated address alone. */
      printf("  dma%u ch%u      mode %02X, address %04X (base %04X),"
             " count %04X (base %04X)\n",
             u + 1u, ch, c->channel[ch].mode, c->channel[ch].current_address,
             c->channel[ch].base_address, c->channel[ch].current_count,
             c->channel[ch].base_count);
    }
  }
  printf("  dma bus      %u bus tick(s), %u asking, %u holding\n",
         board->bus_ticks, board->dma_bus_requests, board->dma_bus_held);
  if (board->dma_transfers > 0u || board->dma_unwired_transfers > 0u) {
    /* Whether the second bus master ever moved anything, and how much of it
     * went to a channel with no peripheral wired. A run that programs a
     * transfer and reports zero here has an arbitration problem rather than a
     * controller one, and nothing else distinguishes those. */
    printf("  dma          %u transfer(s), %u to an unwired channel,"
           " last read %08X wrote %08X\n",
           board->dma_transfers, board->dma_unwired_transfers,
           board->dma_last_read, board->dma_last_write);
  }
  printf("  blit cycles  %u, %u plane write(s)\n", board->graphics_cycles,
         board->graphics_planes_written);
  if (board->graphics_unknown_mode_cycles > 0u) {
    printf("    undescribed CR0 mode %u time(s), first at %08X\n",
           board->graphics_unknown_mode_cycles,
           board->first_graphics_unknown_mode);
  }
  /* Every region the firmware touched, and every one it did not. The zeros are
   * the informative half: a device with no accesses is one the firmware never
   * reached for, which a total cannot say. */
  printf("  regions      reads / writes\n");
  for (unsigned r = 0; r < AP_BOARD_REGIONS; r++) {
    if (board->region_reads[r] == 0u && board->region_writes[r] == 0u) {
      continue;
    }
    printf("    %-22s %8u %8u\n",
           ap_board_region_name((ap_board_region_t)r), board->region_reads[r],
           board->region_writes[r]);
  }
  /* Which serial registers, not just how many. A transmit that never happened
   * and one dropped at the register look identical from a total. */
  for (unsigned unit = 0; unit < 2u; unit++) {
    for (unsigned reg = 0; reg < AP_MC68681_REGISTERS; reg++) {
      if (board->sio.register_writes[unit][reg] == 0u &&
          board->sio.register_reads[unit][reg] == 0u) {
        continue;
      }
      printf("    sio%u reg %-2u %8u write(s) %8u read(s)\n", unit + 1u, reg,
             board->sio.register_writes[unit][reg],
             board->sio.register_reads[unit][reg]);
    }
  }

  if (dump_spec != NULL) {
    uint32_t at = 0, length = 0;
    if (!parse_dump_spec(dump_spec, &at, &length)) {
      fprintf(stderr, "apollo: --dump-mem wants ADDR or ADDR:LEN in hex, not"
                      " %s\n", dump_spec);
    } else {
      printf("memory %08X, %u byte(s), through the board\n", at, length);
      dump_memory(stdout, board, at, length);
    }
  }

  /* Last, so the run's own report is complete first and a failed capture
   * cannot cost the measurements that were already taken. `CR1` is the one the
   * *firmware* programmed, now that the register file stores it -- so `INV` and
   * `DISP_EN` are the machine's own answers rather than the harness's
   * assumption. */
  int status = 0;
  if (screenshot != NULL) {
    status = write_screenshot(screenshot, &board->graphics,
                              board->graphics.reg.cr1);
  }

  free(disk_bytes);
  free(colour_memory);
  free(mono_memory);
  free(board);
  free(trace_ring);
  free(parity);
  free(ram);
  free(prom);
  return status;
}

/* Load a cartridge and run its boot image.
 *
 * The file reading lives here and not in `src/core`, which has no file I/O by
 * design: a deterministic core cannot have a device reaching for a path at run
 * time. The core is handed bytes and never learns where they came from.
 *
 * This is also why the cartridge is a command-line argument rather than a
 * default path. `media/` is gitignored -- Apollo distribution media is not ours
 * to redistribute -- so a build that looked for a cartridge would work on one
 * machine and fail on every other. */
/* Read an `.afd` through the reader, as a driver would.
 *
 * The counterpart of `--tape` for the cartridge and of `--volume` for the disk,
 * and the *reading* path rather than the booting one. Every sector goes through
 * `ap_afd_read` rather than being indexed out of the buffer, which is what makes
 * this a check of the addressing instead of a check of `memcpy`: a wrong
 * geometry, a zero-based sector number or a head-major layout all fail here and
 * none of them fails a size comparison.
 *
 * The image is walked in **cylinder, head, sector** order and the linear
 * numbers it produces must come out 0, 1, 2 ... with no gap and no repeat. That
 * is the whole claim a blank image can support, and it is worth stating plainly
 * that it is not the same claim as "the same image reads identically under
 * both" -- for that the image has to have something in it. */
static int report_floppy(const char *path) {
  long size = 0;
  uint8_t *bytes = read_file(path, &size);
  if (bytes == NULL) {
    fprintf(stderr, "apollo: cannot read floppy image %s\n", path);
    return 1;
  }

  ap_afd_t image;
  if (!ap_afd_open(&image, bytes, (size_t)size, false)) {
    free(bytes);
    fprintf(stderr,
            "apollo: %s is %ld bytes; an Apollo floppy is exactly %u"
            " (%u cylinders x %u heads x %u sectors x %u)\n",
            path, size, (unsigned)AP_AFD_BYTES, AP_AFD_CYLINDERS, AP_AFD_HEADS,
            AP_AFD_SECTORS, AP_AFD_SECTOR_BYTES);
    return 1;
  }

  printf("floppy %s\n", path);
  printf("  bytes        %ld\n", size);
  printf("  geometry     %u cylinders x %u heads x %u sectors x %u bytes\n",
         AP_AFD_CYLINDERS, AP_AFD_HEADS, AP_AFD_SECTORS, AP_AFD_SECTOR_BYTES);
  printf("  sectors      one-based, 1 to %u\n", AP_AFD_SECTORS);

  uint32_t expected = 0;
  uint32_t nonzero = 0;
  bool self_identifying = true;
  uint64_t sum = 0;
  uint8_t sector[AP_AFD_SECTOR_BYTES];
  for (unsigned cyl = 0; cyl < AP_AFD_CYLINDERS; cyl++) {
    for (unsigned head = 0; head < AP_AFD_HEADS; head++) {
      for (unsigned s = 1; s <= AP_AFD_SECTORS; s++) {
        uint32_t lba = 0;
        if (!ap_afd_lba((uint16_t)cyl, (uint8_t)head, (uint8_t)s, &lba)) {
          free(bytes);
          fprintf(stderr, "apollo: %u/%u/%u is not addressable\n", cyl, head, s);
          return 1;
        }
        /* Cylinder-major, then head, then sector: the numbers must come out
         * consecutive. A head-major layout produces every number exactly once
         * too, so a set comparison would pass and this does not. */
        if (lba != expected) {
          free(bytes);
          fprintf(stderr,
                  "apollo: %u/%u/%u is sector %u, expected %u -- the layout is"
                  " not cylinder-major\n",
                  cyl, head, s, lba, expected);
          return 1;
        }
        expected++;
        if (!ap_afd_read(&image, lba, sector)) {
          free(bytes);
          fprintf(stderr, "apollo: sector %u could not be read\n", lba);
          return 1;
        }
        for (unsigned b = 0; b < AP_AFD_SECTOR_BYTES; b++) {
          if (sector[b] != 0u) { nonzero++; }
          sum += sector[b];
        }
        /* An image whose every sector opens with its own linear number checks
         * the mapping rather than merely exercising it: a reader that returned
         * the neighbouring sector would return a wrong *number* instead of
         * plausible bytes. Detected rather than required, so an ordinary image
         * still reports normally. */
        const uint32_t stamp = ((uint32_t)sector[0] << 24) |
                               ((uint32_t)sector[1] << 16) |
                               ((uint32_t)sector[2] << 8) | sector[3];
        if (stamp != lba) { self_identifying = false; }
      }
    }
  }
  printf("  read         %u sectors through the reader, %u non-zero byte(s)\n",
         expected, nonzero);
  printf("  checksum     %016llX\n", (unsigned long long)sum);
  if (self_identifying && nonzero > 0u) {
    printf("               every sector opens with its own linear number, so"
           " the mapping is\n"
           "               checked and not merely exercised\n");
  }
  if (nonzero == 0u) {
    /* Said plainly rather than left to be inferred from a checksum of zero. */
    printf("               the image is blank: this pins the addressing and"
           " nothing about\n"
           "               content, since every sector reads the same under"
           " any geometry\n");
  }
  free(bytes);
  return 0;
}

/* Report a cartridge without requiring it to boot.
 *
 * `--boot-tape` refuses an image with no boot record, which is right for a boot
 * and wrong for the question "does this reader read the distribution media".
 * Four of the five SR10.x cartridges carry data and no `SYSBOOT` header, so the
 * boot path rejects them correctly and says nothing about whether their blocks
 * are readable. This is the reading path, and it is the counterpart of
 * `--volume` for the disk. */
static int report_tape(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open cartridge %s\n", path);
    return 1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "apollo: cannot size cartridge %s\n", path);
    return 1;
  }
  const long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    fprintf(stderr, "apollo: empty cartridge %s\n", path);
    return 1;
  }
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    fprintf(stderr, "apollo: cannot read cartridge %s\n", path);
    return 1;
  }
  fclose(file);

  ap_qic_t drive;
  ap_qic_init(&drive);
  if (!ap_qic_load(&drive, bytes, (size_t)size, AP_QIC_CARTRIDGE_DC600A)) {
    free(bytes);
    fprintf(stderr, "apollo: %s is not a whole number of 512-byte blocks\n",
            path);
    return 1;
  }

  printf("cartridge %s\n", path);
  printf("  bytes        %ld\n", size);
  printf("  blocks       %" PRIu64 "\n", ap_ct_blocks(&drive.image));

  /* Read every block through the drive, as a driver would -- not by indexing
   * the buffer, which would test nothing but `memcpy`. A short or misaddressed
   * image fails here rather than at whatever later step first noticed. */
  if (!ap_qic_command(&drive, AP_QIC_CMD_SELECT) ||
      !ap_qic_command(&drive, AP_QIC_CMD_READ)) {
    free(bytes);
    fprintf(stderr, "apollo: %s could not be selected for reading\n", path);
    return 1;
  }
  uint8_t block[AP_CT_BLOCK_SIZE];
  uint64_t read = 0u;
  while (ap_qic_read_block(&drive, block)) {
    read++;
  }
  printf("  blocks read  %" PRIu64 "%s\n", read,
         read == ap_ct_blocks(&drive.image) ? " (all)" : " -- SHORT");

  ap_ct_boot_image_t boot;
  if (ap_ct_boot_image(&drive.image, &boot)) {
    printf("  boot record  load %08X entry %08X length %u\n",
           boot.load_address, boot.entry_point, boot.length);
  } else {
    printf("  boot record  none -- a data cartridge\n");
  }

  free(bytes);
  return read == ap_ct_blocks(&drive.image) ? 0 : 1;
}

static int boot_from_tape(const char *path, unsigned limit) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "apollo: cannot open cartridge %s\n", path);
    return 1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "apollo: cannot size cartridge %s\n", path);
    return 1;
  }
  long size = ftell(file);
  rewind(file);
  if (size <= 0) {
    fclose(file);
    fprintf(stderr, "apollo: empty cartridge %s\n", path);
    return 1;
  }

  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    fclose(file);
    fprintf(stderr, "apollo: cannot read cartridge %s\n", path);
    return 1;
  }
  fclose(file);

  ap_ct_t cartridge;
  if (!ap_ct_open(&cartridge, bytes, (size_t)size)) {
    free(bytes);
    fprintf(stderr,
            "apollo: %s is not a whole number of %u-byte blocks\n",
            path, AP_CT_BLOCK_SIZE);
    return 1;
  }

  ap_ct_boot_image_t image;
  if (!ap_ct_boot_image(&cartridge, &image)) {
    free(bytes);
    fprintf(stderr, "apollo: %s carries no bootable M68K image\n", path);
    return 1;
  }

  printf("cartridge %s\n", path);
  printf("  blocks       %llu\n", (unsigned long long)ap_ct_blocks(&cartridge));
  printf("  load address %08X\n", image.load_address);
  printf("  entry point  %08X\n", image.entry_point);
  printf("  length       %u\n", image.length);

  /* Enough RAM to hold the image where it belongs, rounded up. The machine
   * takes flat memory from zero; a real DN3500 has its main memory elsewhere,
   * and wiring that is the boot-PROM route this deliberately does not need. */
  /* Main memory's size, not an address: with the board's map the image's load
   * address is in the machine's low regions, not in RAM. */
  uint32_t ram_bytes = 0x400000u;
  uint8_t *ram = calloc(1, ram_bytes);
  if (ram == NULL) {
    free(bytes);
    fprintf(stderr, "apollo: cannot allocate %u bytes of RAM\n", ram_bytes);
    return 1;
  }

  /* The DN3500's own address map, so the firmware meets the machine it was
   * written for: main memory at 1000000, devices at their measured addresses,
   * and an unclaimed address reported rather than answered with zero. */
  static const ap_mc146818_time_t epoch = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  ap_board_t *board = calloc(1, sizeof *board);
  if (board == NULL || !ap_board_init(board, ram, ram_bytes, &epoch,
                                      0x012345u)) {
    free(board);
    free(ram);
    free(bytes);
    fprintf(stderr, "apollo: cannot build the core board\n");
    return 1;
  }

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
  ap_machine_set_board(&machine, board);
  for (uint32_t i = 0; i < image.length; i++) {
    if (!ap_machine_write(&machine, image.load_address + i, 1u,
                          image.data[i])) {
      free(ram);
      free(bytes);
      fprintf(stderr, "apollo: cannot place the image at %08X\n",
              image.load_address);
      return 1;
    }
  }

  /* A stack below the image, which is a choice and not a measurement: the boot
   * PROM would set one and this core is not running the PROM. Reported so that
   * a run which depends on it is visibly depending on a chosen figure. */
  uint32_t stack = image.load_address;
  ap_machine_reset(&machine, image.entry_point, stack);
  printf("  stack        %08X (chosen, not from the cartridge)\n", stack);

  ap_machine_run_t run = ap_machine_run(&machine, limit);
  printf("  executed     %u instruction(s)\n", run.executed);
  printf("  stopped      %s\n", ap_probe_status_name(run.status));
  report_state(&machine);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board, board->first_unmapped_write)));
  }
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  if (board->rom_writes > 0u) {
    printf("    first      %08X\n", board->first_rom_write);
  }
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  if (board->atbus_empty_reads > 0u) {
    printf("    first read %08X\n", board->first_atbus_empty_read);
  }
  if (board->atbus_empty_writes > 0u) {
    printf("    first write %08X\n", board->first_atbus_empty_write);
  }
  /* The two registers Table 2-8 names and this core declines. Printed even at
   * zero for the same reason: "the firmware never touched task alias" is an
   * answer, and the only one available until a handbook turns up. */
  printf("  declined     task alias %u/%u, master request %u/%u (read/write)\n",
         board->task_alias_reads, board->task_alias_writes,
         board->master_request_reads, board->master_request_writes);
  /* The part of the translation map's region no manual describes. Reported even
   * at zero, because zero is the informative answer here: it says the run never
   * went anywhere our assumed decode could be wrong. */
  printf("  atmap undoc  %u read, %u written\n",
         board->atmap_undescribed_reads, board->atmap_undescribed_writes);
  if (board->atmap_undescribed_reads > 0u) {
    printf("    first read %08X\n", board->first_atmap_undescribed_read);
  }
  if (board->atmap_undescribed_writes > 0u) {
    printf("    first write %08X\n", board->first_atmap_undescribed_write);
  }
  printf("  final region %s\n",
         ap_board_region_name(ap_board_region(board, machine.cpu.regs.pc)));

  /* Where it stopped matters more than that it stopped. A fault outside the
   * image is the firmware reaching for hardware that is not mapped, which is
   * expected and is the thing to fix; a fault *inside* the image would mean
   * this core mis-executed something, which is not. */
  if (machine.cpu.regs.pc >= image.load_address &&
      machine.cpu.regs.pc < image.load_address + image.length) {
    printf("  note         PC is inside the loaded image\n");
  } else {
    printf("  note         PC is outside the loaded image\n");
  }

  free(board);
  free(ram);
  free(bytes);
  return 0;
}

int main(int argc, char **argv) {
  const char *program_name = argc > 0 ? argv[0] : "apollo-headless";
  ap_common_options_t opt;
  ap_common_options_init(&opt);

  bool boot_trace = false;
  unsigned boot_trace_last = 0;
  uint32_t boot_stop_pc = 0;
  uint32_t boot_watch = 0;
  const char *boot_input = NULL;
  bool boot_console = false;
  unsigned boot_input_unit = 1u; /* SIO2 */
  unsigned boot_input_channel = 0u;
  /* 9600 baud. **Not** what the firmware configures its own ports to -- that is
   * `77`, 1050 baud, and it is what this defaulted to for a long time on the
   * reasoning that a scripted terminal should match the machine. It should not:
   * the firmware *autobauds*, so the terminal sends at the terminal's rate and
   * the PROM works out which it was. At `77` the negotiation never completes
   * and the machine stays silent; at `BB` it prints its banner. `FINDINGS.md`
   * C113. */
  unsigned boot_input_rate = 0xBBu;
  unsigned boot_input_interval_us = 0u;
  unsigned boot_key = AP_KBD_KEYS; /* none */
  ap_screen_kind_t boot_screen = AP_SCREEN_NONE;
  const char *screenshot = NULL;
  bool run_probe_suite = false;
  const char *probe_file_path = nullptr;
  bool report_timing = false;
  const char *boot_tape = NULL;
  const char *boot_prom = NULL;
  const char *tape_path = NULL;
  /* The node this machine presents. `012345` is what every board in this
   * project has been built with; `--volume` replaces it with the identity the
   * disk itself records, which is the only source that can make a machine and
   * its file system agree. */
  const char *volume_path = NULL;
  const char *floppy_path = NULL;
  const char *disk_path = NULL;
  const char *dump_spec = NULL;
  uint32_t node_id = 0x012345u;
  unsigned boot_limit = 100000u;

  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "--volume") == 0 && i + 1 < argc) {
      volume_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-interval") == 0 && i + 1 < argc) {
      boot_input_interval_us = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--dump-mem") == 0 && i + 1 < argc) {
      dump_spec = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
      disk_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--floppy") == 0 && i + 1 < argc) {
      floppy_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--tape") == 0 && i + 1 < argc) {
      tape_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-prom") == 0 && i + 1 < argc) {
      boot_prom = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-tape") == 0 && i + 1 < argc) {
      boot_tape = argv[i + 1];
      i += 2;
      continue;
    }
    /* Stopping a boot short is how you find where it went wrong. Without it the
     * only observable is the end state, and an end state cannot say which
     * instruction produced it -- a wild PC looks the same however far back the
     * mistake was made. */
    if (strcmp(argv[i], "--boot-limit") == 0 && i + 1 < argc) {
      boot_limit = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-key") == 0 && i + 1 < argc) {
      /* A matrix index, not a character: this keyboard reports keys, and the
       * firmware's own table turns them into characters. */
      boot_key = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-rate") == 0 && i + 1 < argc) {
      boot_input_rate = (unsigned)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-channel") == 0 && i + 1 < argc) {
      const char c = argv[i + 1][0];
      boot_input_channel = (c == 'b' || c == 'B' || c == '1') ? 1u : 0u;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-input-port") == 0 && i + 1 < argc) {
      /* 1 or 2 as the board names them; 0-based inside. */
      const unsigned port = (unsigned)strtoul(argv[i + 1], NULL, 0);
      boot_input_unit = (port >= 2u) ? 1u : 0u;
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
      /* The four the firmware knows, by the names its own probe order
       * implies -- not by ours. */
      const char *name = argv[i + 1];
      if (strcmp(name, "c4p") == 0) {
        boot_screen = AP_SCREEN_COLOUR_4_PLANE;
      } else if (strcmp(name, "c8p") == 0) {
        boot_screen = AP_SCREEN_COLOUR_8_PLANE;
      } else if (strcmp(name, "19i") == 0) {
        boot_screen = AP_SCREEN_MONO_19_INCH;
      } else if (strcmp(name, "15i") == 0) {
        boot_screen = AP_SCREEN_MONO_15_INCH;
      } else {
        fprintf(stderr, "apollo: unknown screen %s (c4p, c8p, 19i, 15i)\n",
                name);
        return 1;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-console") == 0) {
      boot_console = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--boot-input") == 0 && i + 1 < argc) {
      boot_input = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-watch") == 0 && i + 1 < argc) {
      boot_watch = (uint32_t)strtoul(argv[i + 1], NULL, 0);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-stop-pc") == 0 && i + 1 < argc) {
      boot_stop_pc = (uint32_t)strtoul(argv[i + 1], NULL, 16);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-trace-last") == 0 && i + 1 < argc) {
      boot_trace_last = (unsigned)strtoul(argv[i + 1], NULL, 10);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--boot-trace") == 0) {
      boot_trace = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--run-probes") == 0) {
      run_probe_suite = true;
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--probe-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: --probe-file needs a path\n", program_name);
        return 2;
      }
      probe_file_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--time-instructions") == 0) {
      report_timing = true;
      i += 1;
      continue;
    }

    int consumed = 0;
    const char *err = nullptr;
    switch (ap_common_parse_arg(&opt, argc, argv, i, &consumed, &err)) {
      case AP_ARG_CONSUMED:
        i += consumed;
        break;
      case AP_ARG_ERROR:
        fprintf(stderr, "%s: %s: %s\n", program_name, argv[i], err);
        return 2;
      case AP_ARG_UNKNOWN:
        fprintf(stderr, "%s: unrecognised option: %s\n", program_name, argv[i]);
        print_usage(program_name);
        return 2;
    }
  }

  if (opt.help) {
    print_usage(program_name);
    return 0;
  }

  if (opt.list_models) {
    ap_print_model_table(stdout);
    return 0;
  }

  /* Read before anything is built, so a machine is never constructed with an
   * identity that is about to be replaced -- and so a volume that is not one
   * fails before a run rather than during it. */
  if (volume_path != NULL && !node_id_from_volume(volume_path, &node_id)) {
    return 1;
  }

  if (floppy_path != NULL) {
    return report_floppy(floppy_path);
  }

  if (tape_path != NULL) {
    return report_tape(tape_path);
  }

  if (boot_prom != NULL) {
    return boot_from_prom(boot_prom, boot_limit, boot_trace, boot_watch,
                          boot_input, boot_input_unit, boot_input_channel,
                          (uint8_t)boot_input_rate, boot_input_interval_us,
                          boot_key, boot_console,
                          boot_screen, node_id, opt.model->id, screenshot,
                          boot_trace_last, boot_stop_pc, disk_path, dump_spec);
  }

  if (boot_tape != NULL) {
    return boot_from_tape(boot_tape, boot_limit);
  }

  if (probe_file_path != nullptr) {
    return run_probe_file(stdout, opt.model->id, program_name,
                          probe_file_path, dump_spec);
  }

  if (run_probe_suite) {
    run_probes(stdout, opt.model->id);
    return 0;
  }

  if (report_timing) {
    time_instructions(stdout);
    return 0;
  }

  /* A CPU exists and probes run on it, but no *machine* of this model does:
   * there is no I/O, no device and no bus arbitration point, so `--model` has
   * nothing to build yet. Say which is missing rather than implying the whole
   * emulator is absent. */
  fprintf(stderr,
          "%s: cannot run %s yet -- the CPU works, but there are no devices to\n"
          "build a machine from. See docs/PROJECT_STATUS.md; --list-models and\n"
          "--run-probes work now.\n",
          program_name, opt.model->name);
  return 2;
}
