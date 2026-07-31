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

static void print_usage(const char *program_name) {
  ap_print_common_usage(stdout, program_name);
  /* Headless-only flags are listed here as they are implemented. */
  fprintf(stdout,
          "  --run-probes          run the built-in probe suite and report\n");
}

/* The probes' RAM. Static rather than automatic because it is large, and
 * supplied by the caller because the core allocates nothing. */
#define PROBE_RAM_BYTES 0x00010000u
static uint8_t probe_ram[PROBE_RAM_BYTES];

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

int main(int argc, char **argv) {
  const char *program_name = argc > 0 ? argv[0] : "apollo-headless";
  ap_common_options_t opt;
  ap_common_options_init(&opt);

  bool run_probe_suite = false;

  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "--run-probes") == 0) {
      run_probe_suite = true;
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

  if (run_probe_suite) {
    run_probes(stdout);
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
