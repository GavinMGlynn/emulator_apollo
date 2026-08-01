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
#include "board/ap_sio.h"
#include "board/ap_graphics.h"
#include "device/ap_kbd.h"
#include "machine/ap_machine.h"

static void print_usage(const char *program_name) {
  ap_print_common_usage(stdout, program_name);
  /* Headless-only flags are listed here as they are implemented. */
  fprintf(stdout,
          "  --run-probes          run the built-in probe suite and report\n"
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
static int boot_from_prom(const char *path, unsigned limit, bool trace,
                          uint32_t watch, const char *input, unsigned input_unit,
                          unsigned input_channel, uint8_t input_rate,
                          unsigned key, bool console,
                          ap_screen_kind_t screen) {
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
    const ap_board_region_t region = ap_board_region(watch);
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
         ap_board_region_name(ap_board_region(stack)));
  printf("  reset PC     %08X (%s)\n", pc,
         ap_board_region_name(ap_board_region(pc)));

  ap_machine_t machine;
  ap_machine_init(&machine, ram, ram_bytes);
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
      /* Feed the next byte only once the program has taken the last, which is
       * what a terminal's flow looks like and what stops a script from
       * overrunning the receiver. */
      if (input_sent < input_length &&
          !ap_sio_receiver_ready(&board->sio, input_unit, input_channel)) {
        /* Sent at the rate the terminal is set to. `77` is what the DN3500's
         * own firmware configures both ports to at reset, measured off the
         * oracle -- so a scripted terminal that used anything else would be
         * modelling a misconfigured cable rather than a console. */
        ap_sio_receive_at(&board->sio, input_unit, input_channel,
                          (uint8_t)input[input_sent], input_rate);
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
          !ap_sio_receiver_ready(&board->sio, 0u, 0u)) {
        const bool moved = (key_state == 0u) ? ap_board_key_press(board, key)
                                             : ap_board_key_release(board, key);
        if (moved) {
          key_state++;
        }
      }
      const uint32_t step_pc = machine.cpu.regs.pc;
      const ap_m68030_step_result_t r = ap_m68030_step(&machine.cpu);
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
  printf("  final PC     %08X (%s)\n", machine.cpu.regs.pc,
         ap_board_region_name(ap_board_region(machine.cpu.regs.pc)));
  printf("  bus errors   %u\n", machine.bus_errors);
  printf("  unmapped     %u read, %u written\n", board->unmapped_reads,
         board->unmapped_writes);
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board->first_unmapped_write)));
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
  if (board->unmapped_reads > 0u) {
    printf("    first read %08X (%s)\n", board->first_unmapped_read,
           ap_board_region_name(ap_board_region(board->first_unmapped_read)));
  }
  if (board->unmapped_writes > 0u) {
    printf("    first write %08X (%s)\n", board->first_unmapped_write,
           ap_board_region_name(ap_board_region(board->first_unmapped_write)));
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
    return boot_from_prom(boot_prom, boot_limit, boot_trace, boot_watch,
                          boot_input, boot_input_unit, boot_input_channel,
                          (uint8_t)boot_input_rate, boot_key, boot_console,
                          boot_screen);
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
