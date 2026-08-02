/* MC68030 effective address timings, `[030]` §11.6.1 and §11.6.3. See the header
 * for the fetch-against-calculate distinction and for the two notations the
 * tables use that are not numbers. */

#include "cpu/m68030/ap_m68030_ea_timing.h"

/* §11.6.1, Fetch Effective Address. The `(r/p/w)` triple is `(1/0/0)` for every
 * memory mode -- one operand read, which is what makes this the *fetch* table
 * rather than the calculate one. */
static const ap_m68030_ea_timing_t FETCH_REGISTER = {
    "Dn or An", {0, 0, 0, 0, .prefetches = 0}, false, false};
static const ap_m68030_ea_timing_t FETCH_INDIRECT = {
    "(An)", {1, 1, 3, 3, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_POSTINCREMENT = {
    "(An)+", {0, 1, 3, 3, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_PREDECREMENT = {
    "-(An)", {2, 2, 4, 4, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_DISPLACEMENT = {
    "(d16,An) or (d16,PC)", {2, 2, 4, 4, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_ABSOLUTE_SHORT = {
    "(xxx).W", {2, 2, 4, 4, .reads = 1, .prefetches = 0}, true, false};
/* The long absolute is the one row whose two columns differ: 4 to fetch from
 * the cache and 5 without it, because its second extension word is another
 * prefetch. */
static const ap_m68030_ea_timing_t FETCH_ABSOLUTE_LONG = {
    "(xxx).L", {1, 0, 4, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FETCH_INDEXED = {
    "(d8,An,Xn) or (d8,PC,Xn)", {4, 2, 6, 6, .reads = 1, .prefetches = 0}, true, false};

/* The immediate rows are split by operand size, and byte and word cost the
 * same: Table 2-3's "Low-order byte of the extension word" means a byte
 * immediate still occupies a whole word of instruction stream. */
static const ap_m68030_ea_timing_t FETCH_IMMEDIATE_WORD = {
    "#<data>.B or .W", {2, 0, 2, 2, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_IMMEDIATE_LONG = {
    "#<data>.L", {4, 0, 4, 4, .prefetches = 0}, true, false};

/* §11.6.3, Calculate Effective Address. No reads anywhere -- `(0/0/0)` for
 * every row -- which is the whole difference from the fetch table. Several
 * heads are written "2+op head" or "4+op head". */
static const ap_m68030_ea_timing_t CALCULATE_REGISTER = {
    "Dn or An", {0, 0, 0, 0, .prefetches = 0}, false, false};
static const ap_m68030_ea_timing_t CALCULATE_INDIRECT = {
    "(An)", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_POSTINCREMENT = {
    "(An)+", {0, 0, 2, 2, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t CALCULATE_PREDECREMENT = {
    "-(An)", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_DISPLACEMENT = {
    "(d16,An) or (d16,PC)", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_ABSOLUTE_SHORT = {
    "(xxx).W", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_ABSOLUTE_LONG = {
    "(xxx).L", {4, 0, 4, 4, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_INDEXED = {
    "(d8,An,Xn) or (d8,PC,Xn)", {4, 0, 4, 4, .prefetches = 0}, true, true};

unsigned ap_m68030_ea_timing_head(const ap_m68030_ea_timing_t *ea,
                                  unsigned operation_head) {
  if (ea == nullptr || !ea->head_applies) {
    return 0u;
  }
  return ea->timing.head + (ea->head_adds_operation ? operation_head : 0u);
}

void ap_m68030_ea_timing_compose(ap_m68030_overlap_state_t *state,
                                 const ap_m68030_ea_timing_t *ea,
                                 const ap_m68030_timing_t *operation) {
  /* The effective address component first. Skipped entirely for a register
   * operand -- see the header on why a zero-cost component is not the same
   * thing and would over-count. */
  if (ea != nullptr && ea->head_applies) {
    ap_m68030_overlap_add_component(
        state, ap_m68030_ea_timing_head(ea, operation->head), ea->timing.tail,
        ea->timing.cache_case);
  }

  /* Then the operation, which overlaps against whatever tail now precedes it:
   * its own effective address's, or -- when there is none -- the previous
   * instruction's operation. Equation (11-2) writes those as two different
   * terms and they are the same rule. */
  ap_m68030_overlap_add(state, operation);
}

const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_timing(ap_m68030_ea_kind_t kind, unsigned operand_size) {
  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
    return &FETCH_REGISTER;
  case AP_M68030_EA_ADDRESS_INDIRECT:
    return &FETCH_INDIRECT;
  case AP_M68030_EA_POSTINCREMENT:
    return &FETCH_POSTINCREMENT;
  case AP_M68030_EA_PREDECREMENT:
    return &FETCH_PREDECREMENT;
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT:
    return &FETCH_DISPLACEMENT;
  case AP_M68030_EA_ABSOLUTE_SHORT:
    return &FETCH_ABSOLUTE_SHORT;
  case AP_M68030_EA_ABSOLUTE_LONG:
    return &FETCH_ABSOLUTE_LONG;
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    /* The brief format only. A full-format extension word has its own rows,
     * which are a separate pass -- and this returning the brief figure for one
     * would under-count by up to twelve clocks. */
    return &FETCH_INDEXED;
  case AP_M68030_EA_IMMEDIATE:
    return (operand_size == 4u) ? &FETCH_IMMEDIATE_LONG
                                : &FETCH_IMMEDIATE_WORD;
  case AP_M68030_EA_INVALID:
    break;
  }
  return nullptr;
}

const ap_m68030_ea_timing_t *
ap_m68030_ea_calculate_timing(ap_m68030_ea_kind_t kind) {
  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
    return &CALCULATE_REGISTER;
  case AP_M68030_EA_ADDRESS_INDIRECT:
    return &CALCULATE_INDIRECT;
  case AP_M68030_EA_POSTINCREMENT:
    return &CALCULATE_POSTINCREMENT;
  case AP_M68030_EA_PREDECREMENT:
    return &CALCULATE_PREDECREMENT;
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT:
    return &CALCULATE_DISPLACEMENT;
  case AP_M68030_EA_ABSOLUTE_SHORT:
    return &CALCULATE_ABSOLUTE_SHORT;
  case AP_M68030_EA_ABSOLUTE_LONG:
    return &CALCULATE_ABSOLUTE_LONG;
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    return &CALCULATE_INDEXED;
  case AP_M68030_EA_IMMEDIATE:
    /* §11.6.3 has no immediate row: there is no address to calculate for an
     * operand that is in the instruction stream. An instruction footnoted for
     * the calculate table cannot take an immediate, and this says so rather
     * than returning zero, which would read as "free". */
    return nullptr;
  case AP_M68030_EA_INVALID:
    break;
  }
  return nullptr;
}
