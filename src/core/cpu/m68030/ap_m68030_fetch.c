/* MC68030 instruction prefetch. See ap_m68030_fetch.h for why half of all
 * sequential prefetches cost nothing. */

#include "cpu/m68030/ap_m68030_fetch.h"

void ap_m68030_fetch_reset(ap_m68030_fetch_t *fetch, uint32_t address) {
  ap_m68030_pipe_reset(&fetch->pipe);
  fetch->address = address;
  /* `bus_clocks` deliberately survives a reset. A branch empties the pipe; it
   * does not un-spend the clocks already spent fetching, and a counter that
   * restarted at every change of flow would report a fraction of a run's
   * prefetch cost. `ap_m68030_cpu_reset` is where a run's counters begin. */
}

ap_m68030_fetch_result_t ap_m68030_fetch_prefetch(ap_m68030_fetch_t *fetch) {
  ap_m68030_fetch_result_t out = {0};
  const uint32_t address = fetch->address;

  /* The holding register already holds this word's long word, so the prefetch
   * needs no bus cycle and no instruction cache access. */
  if (ap_m68030_pipe_holds(&fetch->pipe, address)) {
    ap_m68030_pipe_load_from_holding(&fetch->pipe, address);
    out.ok = true;
    out.from_holding = true;
    out.clocks = 0;
    fetch->address = address + 2u;
    return out;
  }

  /* Otherwise the long word containing it is fetched through the ordinary
   * access path -- instruction cache first, then the MMU and the bus. */
  const ap_m68030_access_result_t access = ap_m68030_access_read(
      fetch->access, address & ~UINT32_C(3), fetch->function_code);

  if (!access.ok) {
    /* An abnormally terminated fetch still loads the stage, because the fault
     * must be taken where the word is *used* rather than where it was fetched
     * -- which is the rule ap_m68030_pipe already models. */
    ap_m68030_pipe_fill(&fetch->pipe, address, 0, true);
    out.fault = true;
    out.clocks = access.clocks;
    fetch->bus_clocks += access.clocks;
    fetch->address = address + 2u;
    return out;
  }

  ap_m68030_pipe_fill(&fetch->pipe, address, access.value, false);
  out.ok = true;
  out.clocks = access.clocks;
  fetch->bus_clocks += access.clocks;
  fetch->address = address + 2u;
  return out;
}
