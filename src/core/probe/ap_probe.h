/* Probes: small programs run on a constructed machine, reported deterministically.
 *
 * Phase 1 asks for "probes side-loadable into post-boot machine state, so CI
 * needs no copyrighted firmware", verified by "the probe suite runs in CI with
 * `roms/` absent". This is that: a probe is a few instruction words, an entry
 * point and a limit, and running one needs nothing but `ap_machine` and RAM the
 * caller owns.
 *
 * ## Why the results are goldens rather than assertions
 *
 * A unit test asserts what someone expected. A golden pins what the emulator
 * *did*, byte for byte, on every platform and both build types — which is the
 * cross-platform identity claim `docs/PROJECT_STATUS.md` makes, and the only
 * mechanism that catches one platform quietly disagreeing with the other three.
 * `cmake/Goldens.cmake` already runs that check for every build preset.
 *
 * A probe therefore reports rather than judges. It says how far it got, why it
 * stopped, what the clock came to and what the whole machine hashed to; nothing
 * in this module knows what any of those *should* be.
 *
 * ## What the clock in a probe result means today
 *
 * Bus and cache time only. `ap_m68030_step` does not yet include the microcode
 * clocks an instruction takes between its bus cycles — a named plan item, gated
 * on measurement — so a probe's clock is a lower bound rather than a figure to
 * compare against `[030]` §11.6. It is reported anyway, because pinning it now
 * is what makes the change visible when those clocks arrive: the golden moves,
 * and the diff says by how much for every probe at once.
 */

#ifndef APOLLO_PROBE_AP_PROBE_H
#define APOLLO_PROBE_AP_PROBE_H

#include <stdint.h>

#include "cpu/m68030/ap_m68030_step.h"

typedef struct {
  const char *name;
  /* What the probe is *for*, printed with the result. A probe whose purpose
   * lives only in someone's head becomes unmaintainable the first time its
   * golden moves and nobody can say whether the new number is better. */
  const char *purpose;

  const uint16_t *words;
  unsigned word_count;

  uint32_t load_address;
  uint32_t entry;
  uint32_t stack;

  /* Instructions, not clocks: a probe that runs away must end as a failed probe
   * rather than as a hung harness, and the count is what the report shows. */
  unsigned limit;
} ap_probe_t;

typedef struct {
  unsigned executed;
  ap_m68030_step_status_t status;
  uint64_t clocks;
  uint64_t hash;
  unsigned bus_errors;
  /* Registers worth reporting without printing all sixteen: D0 is where most
   * probes leave their answer, and the PC says where it stopped. */
  uint32_t d0;
  uint32_t pc;
} ap_probe_result_t;

/* Run one probe over caller-owned RAM. The RAM is blanked first, so a probe's
 * result cannot depend on what the previous probe left — which is the whole
 * reason a probe suite is worth running in a fixed order. */
[[nodiscard]] ap_probe_result_t ap_probe_run(const ap_probe_t *probe,
                                             uint8_t *ram, uint32_t ram_bytes);

/* The built-in set, in a fixed order. */
[[nodiscard]] const ap_probe_t *ap_probe_all(unsigned *count);

/* The status as a short, stable word for the report. Stable because a golden
 * diff is read by a person: "UNIMPLEMENTED" says what happened where a bare
 * enumerator value would need looking up. */
[[nodiscard]] const char *ap_probe_status_name(ap_m68030_step_status_t status);

#endif /* APOLLO_PROBE_AP_PROBE_H */
