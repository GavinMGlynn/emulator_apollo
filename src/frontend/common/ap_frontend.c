#include "ap_frontend.h"

#include <string.h>

#include "time/ap_time.h"

/* Every one of these switches is total over its enumeration and has no default
 * case, so -Wswitch-enum turns "added a model variant, forgot to name it" into
 * a build failure. The trailing return exists only because C does not know the
 * switch is exhaustive. */

const char *ap_cpu_name(ap_cpu_t cpu) {
  switch (cpu) {
    case AP_CPU_M68020: return "68020";
    case AP_CPU_M68030: return "68030";
    case AP_CPU_M68040: return "68040";
  }
  return "unknown";
}

const char *ap_mmu_name(ap_mmu_t mmu) {
  switch (mmu) {
    case AP_MMU_M68851: return "68851 external";
    case AP_MMU_M68030: return "68030 on-chip";
    case AP_MMU_M68040: return "68040 on-chip";
  }
  return "unknown";
}

const char *ap_fpu_name(ap_fpu_t fpu) {
  switch (fpu) {
    case AP_FPU_M68881: return "68881";
    case AP_FPU_M68882: return "68882";
    case AP_FPU_M68040: return "68040 integrated";
  }
  return "unknown";
}

const char *ap_display_name(ap_display_t display) {
  switch (display) {
    case AP_DISPLAY_NONE:           return "headless";
    case AP_DISPLAY_MONO_1024X800:  return "mono 1024x800";
    case AP_DISPLAY_MONO_1280X1024: return "mono 1280x1024";
    case AP_DISPLAY_COLOR_1280X1024: return "color 1280x1024";
    case AP_DISPLAY_COLOR_1024X800: return "color 1024x800";
  }
  return "unknown";
}

const char *ap_oracle_name(ap_oracle_t oracle) {
  switch (oracle) {
    case AP_ORACLE_MAME:       return "mame";
    case AP_ORACLE_PAPER_ONLY: return "paper";
  }
  return "unknown";
}

void ap_print_model_table(FILE *out) {
  fprintf(out, "%-8s  %-6s  %-8s  %-15s  %-15s  %-6s  %s\n", "model", "cpu",
          "clock", "mmu", "display", "oracle", "ram");
  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    ap_clock_t clk;
    /* A model whose clock the time base cannot represent exactly would drift
     * against the ring. Surface it here rather than let it pass silently. */
    bool exact = ap_clock_init(&clk, m->cpu_hz);
    fprintf(out, "%-8s  %-6s  %2u MHz%s  %-15s  %-15s  %-6s  %u MB @ %#010x%s\n",
            m->name, ap_cpu_name(m->cpu), m->cpu_hz / 1000000u,
            exact ? "  " : " !", ap_mmu_name(m->mmu), ap_display_name(m->display),
            ap_oracle_name(m->oracle), m->ram_max_bytes / (1024u * 1024u),
            m->ram_base, m->provisional != nullptr ? "  PROVISIONAL" : "");
  }

  fprintf(out, "\ntime base: %llu Hz\n", (unsigned long long)AP_TIME_BASE_HZ);

  for (size_t i = 0; i < ap_model_count(); ++i) {
    const ap_model_t *m = ap_model_by_id((ap_model_id_t)i);
    if (m->provisional != nullptr) {
      fprintf(out, "PROVISIONAL %s: %s\n", m->name, m->provisional);
    }
  }
}

void ap_common_options_init(ap_common_options_t *opt) {
  opt->model = ap_model_by_name(AP_DEFAULT_MODEL_NAME);
  opt->list_models = false;
  opt->help = false;
}

ap_arg_result_t ap_common_parse_arg(ap_common_options_t *opt, int argc,
                                   char **argv, int i, int *consumed,
                                   const char **err) {
  *consumed = 0;
  *err = nullptr;
  if (i < 0 || i >= argc || argv[i] == nullptr) {
    *err = "argument index out of range";
    return AP_ARG_ERROR;
  }

  const char *arg = argv[i];

  if (strcmp(arg, "--list-models") == 0) {
    opt->list_models = true;
    *consumed = 1;
    return AP_ARG_CONSUMED;
  }

  if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
    opt->help = true;
    *consumed = 1;
    return AP_ARG_CONSUMED;
  }

  if (strcmp(arg, "--model") == 0) {
    if (i + 1 >= argc || argv[i + 1] == nullptr) {
      *err = "--model requires a model name";
      return AP_ARG_ERROR;
    }
    const ap_model_t *m = ap_model_by_name(argv[i + 1]);
    if (m == nullptr) {
      *err = "unknown model name; try --list-models";
      return AP_ARG_ERROR;
    }
    opt->model = m;
    *consumed = 2;
    return AP_ARG_CONSUMED;
  }

  return AP_ARG_UNKNOWN;
}

void ap_print_common_usage(FILE *out, const char *program_name) {
  fprintf(out,
          "usage: %s [options]\n"
          "\n"
          "common options:\n"
          "  --model NAME     machine to emulate (default %s)\n"
          "  --list-models    print the model table and exit\n"
          "  -h, --help       print this help and exit\n",
          program_name, AP_DEFAULT_MODEL_NAME);
}
