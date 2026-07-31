/* Deterministic frontend: no wall clock, no host input, no threads.
 *
 * Presently it can only report the machine configuration it would build. The
 * flags that make this the engine of the verification methodology (run N
 * cycles, dump state, --dump-mem, screenshots, scripted input, ring trace) are
 * added alongside the subsystems they observe. */

#include <stdio.h>
#include <string.h>

#include "model/ap_model.h"
#include "time/ap_time.h"

static const char *cpu_name(ap_cpu_t cpu) {
  switch (cpu) {
    case AP_CPU_M68020: return "68020";
    case AP_CPU_M68030: return "68030";
    case AP_CPU_M68040: return "68040";
  }
  return "?";
}

static const char *display_name(ap_display_t d) {
  switch (d) {
    case AP_DISPLAY_NONE:            return "headless";
    case AP_DISPLAY_MONO_1024X800:   return "mono 1024x800";
    case AP_DISPLAY_MONO_1280X1024:  return "mono 1280x1024";
    case AP_DISPLAY_COLOR_1024X800:  return "color 1024x800";
  }
  return "?";
}

static void print_models(void) {
  printf("%-8s  %-6s  %-8s  %-14s  %-8s  %s\n", "model", "cpu", "clock",
         "display", "oracle", "ram");
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    ap_clock_t clk;
    /* A model whose clock the time base cannot represent exactly is a
     * configuration bug, and must be visible as one. */
    bool ok = ap_clock_init(&clk, m->cpu_hz);
    printf("%-8s  %-6s  %2u MHz%s  %-14s  %-8s  %u MB @ %#010x%s\n",
           m->name, cpu_name(m->cpu), m->cpu_hz / 1000000u,
           ok ? "  " : " !", display_name(m->display),
           m->oracle == AP_ORACLE_MAME ? "mame" : "paper",
           m->ram_max_bytes / (1024u * 1024u), m->ram_base,
           m->provisional != nullptr ? "  PROVISIONAL" : "");
  }
  printf("\ntime base: %llu Hz\n", (unsigned long long)AP_TIME_BASE_HZ);
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    if (m->provisional != nullptr) {
      printf("PROVISIONAL %s: %s\n", m->name, m->provisional);
    }
  }
}

int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "--list-models") == 0) {
    print_models();
    return 0;
  }
  fprintf(stderr,
          "apollo-headless: deterministic Apollo Domain frontend\n"
          "usage: %s --list-models\n",
          argc > 0 ? argv[0] : "apollo-headless");
  return 2;
}
