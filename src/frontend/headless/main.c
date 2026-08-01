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
#include "board/ap_board.h"
#include "machine/ap_machine.h"

static void print_usage(const char *program_name) {
  ap_print_common_usage(stdout, program_name);
  /* Headless-only flags are listed here as they are implemented. */
  fprintf(stdout,
          "  --run-probes          run the built-in probe suite and report\n"
          "  --time-instructions   report per-instruction clocks, for oracle\n"
          "                        comparison\n"
          "  --boot-limit N        stop a boot after N instructions, to find\n"
          "                        where one goes wrong\n");
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
static void run_probes(FILE *out) {
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
        ap_probe_run(&probes[i], probe_ram, PROBE_RAM_BYTES);
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

/* Run the machine from its boot PROM, which is how a DN3500 actually starts.
 *
 * The side-loading route (`--boot-tape`) puts an image at an address it names
 * and jumps there. That works only while memory answers everywhere: the real
 * machine has no physical memory at the boot image's load address, and the
 * addresses in its header are logical (`FINDINGS.md` C28). The PROM is what
 * enables translation, so it is what has to run first. */
static int boot_from_prom(const char *path, unsigned limit) {
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
      !ap_board_init(board, ram, ram_bytes, &epoch, 0x012345u)) {
    free(board);
    free(ram);
    free(prom);
    fprintf(stderr, "apollo: cannot build the core board\n");
    return 1;
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

  printf("boot PROM %s\n", path);
  printf("  size         %lu\n", (unsigned long)size);
  printf("  reset SSP    %08X (%s)\n", stack,
         ap_board_region_name(ap_board_region(stack)));
  printf("  reset PC     %08X (%s)\n", pc,
         ap_board_region_name(ap_board_region(pc)));

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
  ap_machine_set_board(&machine, board);
  ap_machine_reset(&machine, pc, stack);

  ap_machine_run_t run = ap_machine_run(&machine, limit);
  printf("  executed     %u instruction(s)\n", run.executed);
  printf("  stopped      %s\n", ap_probe_status_name(run.status));
  printf("  final PC     %08X (%s)\n", machine.cpu.regs.pc,
         ap_board_region_name(ap_board_region(machine.cpu.regs.pc)));
  printf("  bus errors   %u\n", machine.bus_errors);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  printf("  state hash   %016llX\n",
         (unsigned long long)ap_machine_hash(&machine));

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
  printf("  state hash   %016llX\n",
         (unsigned long long)ap_machine_hash(&machine));
  printf("  final PC     %08X\n", machine.cpu.regs.pc);
  printf("  bus errors   %u\n", machine.bus_errors);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  /* Reported separately because it is not an error: a read-only memory absorbs
   * a write, and this firmware is known to make one. Folded into the unmapped
   * total it would read as a fault that never happened. */
  printf("  rom writes   %u\n", board->rom_writes);
  printf("  empty slot   %u read, %u written\n", board->atbus_empty_reads,
         board->atbus_empty_writes);
  printf("  final region %s\n",
         ap_board_region_name(ap_board_region(machine.cpu.regs.pc)));

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

  bool run_probe_suite = false;
  bool report_timing = false;
  const char *boot_tape = NULL;
  const char *boot_prom = NULL;
  unsigned boot_limit = 100000u;

  for (int i = 1; i < argc;) {
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
    if (strcmp(argv[i], "--run-probes") == 0) {
      run_probe_suite = true;
      i += 1;
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

  if (boot_prom != NULL) {
    return boot_from_prom(boot_prom, boot_limit);
  }

  if (boot_tape != NULL) {
    return boot_from_tape(boot_tape, boot_limit);
  }

  if (run_probe_suite) {
    run_probes(stdout);
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
