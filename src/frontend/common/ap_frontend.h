/* Code shared by every frontend.
 *
 * The deterministic headless frontend and the interactive SDL frontend differ
 * only in how they present a running machine and where their input comes from.
 * Everything before that -- naming the machine's parts, reporting the model
 * table, and parsing the options both accept -- belongs here, so the two cannot
 * drift into disagreeing about what `--model dn3500` means.
 *
 * This is a frontend concern, not a core one: `src/core` has no opinion about
 * argv or about how an enumeration is spelled for a human.
 */

#ifndef APOLLO_FRONTEND_COMMON_AP_FRONTEND_H
#define APOLLO_FRONTEND_COMMON_AP_FRONTEND_H

#include <stdbool.h>
#include <stdio.h>

#include "model/ap_model.h"

/* Human-readable names. Total over their enumerations: adding a variant without
 * naming it is a compile error, not a "?" at run time. */
[[nodiscard]] const char *ap_cpu_name(ap_cpu_t cpu);
[[nodiscard]] const char *ap_mmu_name(ap_mmu_t mmu);
[[nodiscard]] const char *ap_fpu_name(ap_fpu_t fpu);
[[nodiscard]] const char *ap_display_name(ap_display_t display);
[[nodiscard]] const char *ap_oracle_name(ap_oracle_t oracle);

/* The `--list-models` report: the model table as the code holds it, including
 * the time base and every figure still PROVISIONAL. Identical in every
 * frontend, and deterministic -- it is compared across build types in CI. */
void ap_print_model_table(FILE *out);

/* Options every frontend accepts. A frontend's own flags -- `--frames` for SDL,
 * `--dump-mem` for headless -- are parsed by that frontend. */
typedef struct {
  const ap_model_t *model; /* never NULL after init; defaults to dn3500 */
  bool list_models;
  bool help;
} ap_common_options_t;

/* DN3500 is the reference superset, so it is the default machine. */
#define AP_DEFAULT_MODEL_NAME "dn3500"

void ap_common_options_init(ap_common_options_t *opt);

typedef enum {
  AP_ARG_CONSUMED, /* a common flag; advance argv by *consumed */
  AP_ARG_UNKNOWN,  /* not ours -- the frontend may still claim it */
  AP_ARG_ERROR     /* ours, but malformed; *err explains */
} ap_arg_result_t;

/* Parse the single argument at argv[i].
 *
 * Deliberately one argument at a time rather than a whole-argv parser: each
 * frontend interleaves its own flags with these, and a shared parser that owned
 * the whole loop would have to know about flags it has no business knowing.
 *
 * On AP_ARG_CONSUMED, *consumed is 1 or 2 (a flag, or a flag and its value).
 * On AP_ARG_ERROR, *err points to a static message. */
[[nodiscard]] ap_arg_result_t ap_common_parse_arg(ap_common_options_t *opt,
                                                 int argc, char **argv, int i,
                                                 int *consumed,
                                                 const char **err);

/* The usage text for the flags above, so both frontends document them the same
 * way. A frontend prints its own flags after this. */
void ap_print_common_usage(FILE *out, const char *program_name);

#endif /* APOLLO_FRONTEND_COMMON_AP_FRONTEND_H */
