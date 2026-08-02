/* MC68040 instruction timing: §10.1's pipeline model and Table 10-2. See the
 * header for why this is not the 68030's composition with different numbers. */

#include "cpu/m68040/ap_m68040_timing.h"

unsigned ap_m68040_ea_accesses_fetching(ap_m68040_ea_t ea) {
  switch (ea) {
  /* Nothing in memory to reach. */
  case AP_M68040_EA_DATA_REGISTER:
  case AP_M68040_EA_ADDRESS_REGISTER:
  case AP_M68040_EA_IMMEDIATE:
    return 0u;

  /* One access for the operand. */
  case AP_M68040_EA_INDIRECT:
  case AP_M68040_EA_POSTINCREMENT:
  case AP_M68040_EA_PREDECREMENT:
  case AP_M68040_EA_DISPLACEMENT:
  case AP_M68040_EA_PC_DISPLACEMENT:
  case AP_M68040_EA_ABSOLUTE:
  case AP_M68040_EA_INDEXED:
  case AP_M68040_EA_PC_INDEXED:
  case AP_M68040_EA_BASE_INDEXED:
  case AP_M68040_EA_BASE_DISPLACEMENT:
    return 1u;

  /* Two: the indirect long word, then the operand. */
  case AP_M68040_EA_MEMORY_PREINDEXED:
  case AP_M68040_EA_MEMORY_PREINDEXED_OD:
  case AP_M68040_EA_MEMORY_POSTINDEXED:
  case AP_M68040_EA_MEMORY_POSTINDEXED_OD:
    return 2u;

  case AP_M68040_EA_COUNT:
    break;
  }
  return 0u;
}

unsigned ap_m68040_ea_accesses_sending(ap_m68040_ea_t ea) {
  switch (ea) {
  /* Only the memory indirect modes read anything when the operand itself is
   * not fetched -- they still have to follow the indirection to know the
   * address they are handing on. Every other mode computes its address from
   * registers and extension words alone. */
  case AP_M68040_EA_MEMORY_PREINDEXED:
  case AP_M68040_EA_MEMORY_PREINDEXED_OD:
  case AP_M68040_EA_MEMORY_POSTINDEXED:
  case AP_M68040_EA_MEMORY_POSTINDEXED_OD:
    return 1u;

  case AP_M68040_EA_DATA_REGISTER:
  case AP_M68040_EA_ADDRESS_REGISTER:
  case AP_M68040_EA_INDIRECT:
  case AP_M68040_EA_POSTINCREMENT:
  case AP_M68040_EA_PREDECREMENT:
  case AP_M68040_EA_DISPLACEMENT:
  case AP_M68040_EA_PC_DISPLACEMENT:
  case AP_M68040_EA_ABSOLUTE:
  case AP_M68040_EA_IMMEDIATE:
  case AP_M68040_EA_INDEXED:
  case AP_M68040_EA_PC_INDEXED:
  case AP_M68040_EA_BASE_INDEXED:
  case AP_M68040_EA_BASE_DISPLACEMENT:
  case AP_M68040_EA_COUNT:
    break;
  }
  return 0u;
}

unsigned ap_m68040_fetch_clocks(unsigned accesses) {
  /* "One clock in the <ea> fetch stage for each memory access", floored at one:
   * "an instruction requires one clock to pass through the <ea> fetch stage
   * even if no operand is fetched." The floor is the part a naive reading
   * drops -- `Dn` costs zero accesses and one clock. */
  return (accesses == 0u) ? 1u : accesses;
}

unsigned ap_m68040_execute_total(ap_m68040_execute_t execute) {
  return execute.lead + execute.base;
}

unsigned ap_m68040_interlock_penalty(ap_m68040_execute_t execute,
                                     unsigned stall) {
  /* "If an instruction ... is stalled for more than nL clocks waiting to begin
   * execution in the execute stage, a similar increase in the <ea> calculate
   * time will result." So the lead absorbs stalls up to its own size and the
   * penalty is whatever is left over. */
  return (stall > execute.lead) ? (stall - execute.lead) : 0u;
}

ap_m68040_execute_t ap_m68040_pc_relative_execute(
    ap_m68040_execute_t execute) {
  /* "1 and 1L clocks to the <ea> calculate and execution times": the execution
   * addition is to the *lead*, so it buys more stall tolerance rather than
   * costing a clock outright. */
  execute.lead += 1u;
  return execute;
}

bool ap_m68040_ea_is_interlocked(ap_m68040_ea_t ea) {
  /* "All instructions using the brief and full extension word formats" -- the
   * indexed modes and everything built on them. The simple modes carry no
   * extension word of that kind and so are not interlocked. */
  switch (ea) {
  case AP_M68040_EA_INDEXED:
  case AP_M68040_EA_PC_INDEXED:
  case AP_M68040_EA_BASE_INDEXED:
  case AP_M68040_EA_BASE_DISPLACEMENT:
  case AP_M68040_EA_MEMORY_PREINDEXED:
  case AP_M68040_EA_MEMORY_PREINDEXED_OD:
  case AP_M68040_EA_MEMORY_POSTINDEXED:
  case AP_M68040_EA_MEMORY_POSTINDEXED_OD:
    return true;

  case AP_M68040_EA_DATA_REGISTER:
  case AP_M68040_EA_ADDRESS_REGISTER:
  case AP_M68040_EA_INDIRECT:
  case AP_M68040_EA_POSTINCREMENT:
  case AP_M68040_EA_PREDECREMENT:
  case AP_M68040_EA_DISPLACEMENT:
  case AP_M68040_EA_PC_DISPLACEMENT:
  case AP_M68040_EA_ABSOLUTE:
  case AP_M68040_EA_IMMEDIATE:
  case AP_M68040_EA_COUNT:
    break;
  }
  return false;
}
