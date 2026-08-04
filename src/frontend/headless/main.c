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

#include <stdio.h>
#include <string.h>

#include "ap_frontend.h"
#include "probe/ap_probe.h"

#include <stdlib.h>

#include "image/ap_ct.h"
#include "image/ap_volume.h"
#include "board/ap_board.h"
#include "board/ap_sio.h"
#include "board/ap_graphics.h"
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
          "                        (default 0x77, what the firmware configures)\n"
          "  --boot-key N          press and release keyboard key N (a matrix\n"
          "                        index 0-7F, not a character)\n"
          "  --screen KIND         fit a display: c4p, c8p, 19i or 15i\n"
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
static int run_probe_file(FILE *out, ap_model_id_t model,
                          const char *program_name,
                          const char *path) {
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
  printf("  bus errors   %u\n", state.bus_errors);
}

/* Run the machine from its boot PROM, which is how a DN3500 actually starts.
 *
 * The side-loading route (`--boot-tape`) puts an image at an address it names
 * and jumps there. That works only while memory answers everywhere: the real
 * machine has no physical memory at the boot image's load address, and the
 * addresses in its header are logical (`FINDINGS.md` C28). The PROM is what
 * enables translation, so it is what has to run first. */
static int boot_from_prom(const char *path, unsigned limit, bool trace,
                          uint32_t watch, const char *input, unsigned input_unit,
                          unsigned input_channel, uint8_t input_rate,
                          unsigned key, bool console,
                          ap_screen_kind_t screen, uint32_t node_id,
                          ap_model_id_t model) {
  long size = 0;
  uint8_t *prom = read_file(path, &size);
  if (prom == NULL) {
    fprintf(stderr, "apollo: cannot read boot PROM %s\n", path);
    return 1;
  }

  uint32_t ram_bytes = 0x400000u;
  uint8_t *ram = calloc(1, ram_bytes);
  ap_board_t *board = calloc(1, sizeof *board);
  static const ap_mc146818_time_t epoch = {
      .year = 1987u, .month = 7u, .day = 31u, .day_of_week = 6u,
      .hour = 21u, .minute = 9u, .second = 21u,
  };
  if (ram == NULL || board == NULL ||
      !ap_board_init_model(board, ram, ram_bytes, &epoch, node_id, model)) {
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
    const uint32_t colour_bytes =
        AP_GRAPHICS_COLOUR_MEMORY_END - AP_GRAPHICS_COLOUR_MEMORY_ADDR + 1u;
    const uint32_t mono_bytes =
        AP_GRAPHICS_MONO_MEMORY_END - AP_GRAPHICS_MONO_MEMORY_ADDR + 1u;
    colour_memory = calloc(1, colour_bytes);
    mono_memory = calloc(1, mono_bytes);
    if (colour_memory == NULL || mono_memory == NULL) {
      free(colour_memory);
      free(mono_memory);
      free(board);
      free(ram);
      free(prom);
      fprintf(stderr, "apollo: cannot allocate the graphics memories\n");
      return 1;
    }
    ap_graphics_init(&board->graphics, screen);
    ap_graphics_attach_memory(&board->graphics, colour_memory, colour_bytes,
                              mono_memory, mono_bytes);
  }

  if (!ap_board_load_prom(board, prom, (uint32_t)size)) {
    free(board);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: %s does not fit the boot PROM region\n", path);
    return 1;
  }

  uint32_t stack = 0;
  uint32_t pc = 0;
  if (!ap_board_reset_vector(board, &stack, &pc)) {
    free(board);
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
   * imposes: a start bit, eight data bits and a stop bit cannot be delivered
   * closer together than ten bit times, whatever the far end is doing.
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
  const ap_time_t input_interval =
      input_baud != 0u ? (AP_TIME_BASE_HZ * 10u) / input_baud : 0u;
  ap_time_t input_next_at = 0u;

  ap_machine_run_t run;
  if (trace || input_length > 0u || console || key < AP_KBD_KEYS) {
    /* Step by step, reporting the program counter and the active stack pointer.
     *
     * A7 is the observable this exists for. A wrong PC is where damage becomes
     * visible; a stack pointer that stops matching the call depth is where it
     * happens, and the two can be thousands of instructions apart. Printing
     * both together is what lets one be found from the other. */
    run = (ap_machine_run_t){.status = AP_M68030_STEP_EXECUTED};
    if (trace) {
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
      if (!trace) {
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

  free(board);
  free(ram);
  free(prom);
  return 0;
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
  uint32_t boot_watch = 0;
  const char *boot_input = NULL;
  bool boot_console = false;
  unsigned boot_input_unit = 1u; /* SIO2 */
  unsigned boot_input_channel = 0u;
  unsigned boot_input_rate = 0x77u;
  unsigned boot_key = AP_KBD_KEYS; /* none */
  ap_screen_kind_t boot_screen = AP_SCREEN_NONE;
  bool run_probe_suite = false;
  const char *probe_file_path = nullptr;
  bool report_timing = false;
  const char *boot_tape = NULL;
  const char *boot_prom = NULL;
  /* The node this machine presents. `012345` is what every board in this
   * project has been built with; `--volume` replaces it with the identity the
   * disk itself records, which is the only source that can make a machine and
   * its file system agree. */
  const char *volume_path = NULL;
  uint32_t node_id = 0x012345u;
  unsigned boot_limit = 100000u;

  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "--volume") == 0 && i + 1 < argc) {
      volume_path = argv[i + 1];
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

  if (boot_prom != NULL) {
    return boot_from_prom(boot_prom, boot_limit, boot_trace, boot_watch,
                          boot_input, boot_input_unit, boot_input_channel,
                          (uint8_t)boot_input_rate, boot_key, boot_console,
                          boot_screen, node_id, opt.model->id);
  }

  if (boot_tape != NULL) {
    return boot_from_tape(boot_tape, boot_limit);
  }

  if (probe_file_path != nullptr) {
    return run_probe_file(stdout, opt.model->id, program_name,
                          probe_file_path);
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
