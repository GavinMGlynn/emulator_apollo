/* MC68030 effective address timings, `[030]` §11.6.1 and §11.6.3. See the header
 * for the fetch-against-calculate distinction and for the two notations the
 * tables use that are not numbers. */

#include "cpu/m68030/ap_m68030_ea_timing.h"

/* §11.6.1, Fetch Effective Address. The cache case's `(r/p/w)` triple is
 * `(1/0/0)` for every memory mode -- one operand read, which is what makes this
 * the *fetch* table rather than the calculate one.
 *
 * The `p` counts below are the **no-cache** column's, and five of them were
 * transcribed as zero until the page image was read: `(d16,An)`, `(xxx).W`, the
 * brief-format indexed row and both immediates are `(1/1/0)` or `(0/1/0)` there
 * against `(1/0/0)` or `(0/0/0)` cached. Their totals are the same in both
 * columns, which is the point -- an effective address calculation has enough
 * microcode to hide its own extension word's fetch -- and a `p` of zero would
 * have said there was no fetch to hide.
 *
 * Found by going to the page rather than to the text extraction, which had
 * rendered `4(1/1/0)` as `4(1/010)` and lost the distinction. */
static const ap_m68030_ea_timing_t FETCH_REGISTER = {
    "Dn or An", {0, 0, 0, 0, .prefetches = 0}, false, false};
static const ap_m68030_ea_timing_t FETCH_INDIRECT = {
    "(An)", {1, 1, 3, 3, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_POSTINCREMENT = {
    "(An)+", {0, 1, 3, 3, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_PREDECREMENT = {
    "-(An)", {2, 2, 4, 4, .reads = 1, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t FETCH_DISPLACEMENT = {
    "(d16,An) or (d16,PC)", {2, 2, 4, 4, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FETCH_ABSOLUTE_SHORT = {
    "(xxx).W", {2, 2, 4, 4, .reads = 1, .prefetches = 1}, true, false};
/* The long absolute is the one row whose two columns differ: 4 to fetch from
 * the cache and 5 without it, because its second extension word is another
 * prefetch. */
static const ap_m68030_ea_timing_t FETCH_ABSOLUTE_LONG = {
    "(xxx).L", {1, 0, 4, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FETCH_INDEXED = {
    "(d8,An,Xn) or (d8,PC,Xn)", {4, 2, 6, 6, .reads = 1, .prefetches = 1}, true, false};

/* The immediate rows are split by operand size, and byte and word cost the
 * same: Table 2-3's "Low-order byte of the extension word" means a byte
 * immediate still occupies a whole word of instruction stream. */
static const ap_m68030_ea_timing_t FETCH_IMMEDIATE_WORD = {
    "#<data>.B or .W", {2, 0, 2, 2, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FETCH_IMMEDIATE_LONG = {
    "#<data>.L", {4, 0, 4, 4, .prefetches = 1}, true, false};

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
    "(d16,An) or (d16,PC)", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_ABSOLUTE_SHORT = {
    "(xxx).W", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_ABSOLUTE_LONG = {
    "(xxx).L", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CALCULATE_INDEXED = {
    "(d8,An,Xn) or (d8,PC,Xn)", {4, 0, 4, 4, .prefetches = 1}, true, true};

/* §11.6.1's FULL FORMAT EXTENSION WORD(S) rows, transcribed from the page image.
 * Sixteen entries: four base-displacement cases against four outer-displacement
 * cases, which is the whole space a full-format extension word can express.
 *
 * The row names are the manual's own, so a figure can be traced to a line. Where
 * a row is named twice in the table -- `([B])` and `([B],I)` are both
 * `10(2/0/0)`, the index making no difference -- the name here is the first.
 *
 * `PROVISIONAL`: which of the table's two groups an encoding selects is a
 * reading, not a statement. See the header. */

/* No memory indirect action. */
static const ap_m68030_ea_timing_t FULL_NONE_BD_NULL = {
    "(B)", {4, 0, 6, 7, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FULL_NONE_BD_WORD_BASED = {
    "(d16,An) or (d16,PC)", {2, 0, 6, 7, .reads = 1, .prefetches = 1}, true,
    false};
static const ap_m68030_ea_timing_t FULL_NONE_BD_WORD = {
    "(d16,B)", {4, 0, 8, 10, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FULL_NONE_BD_LONG = {
    "(d32,B)", {4, 0, 12, 13, .reads = 1, .prefetches = 2}, true, false};

/* Memory indirect, no outer displacement. */
static const ap_m68030_ea_timing_t FULL_OD_NULL_BD_NULL = {
    "([B])", {4, 0, 10, 10, .reads = 2, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_NULL_BD_WORD_BASED = {
    "([d16,An]) or ([d16,PC])", {2, 0, 10, 10, .reads = 2, .prefetches = 1},
    true, false};
static const ap_m68030_ea_timing_t FULL_OD_NULL_BD_WORD = {
    "([d16,B])", {4, 0, 12, 13, .reads = 2, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_NULL_BD_LONG = {
    "([d32,B])", {4, 0, 16, 17, .reads = 2, .prefetches = 2}, true, false};

/* Memory indirect with a word outer displacement. */
static const ap_m68030_ea_timing_t FULL_OD_WORD_BD_NULL = {
    "([B],d16)", {4, 0, 12, 13, .reads = 2, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_WORD_BD_WORD_BASED = {
    "([d16,An],d16)", {2, 0, 12, 13, .reads = 2, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_WORD_BD_WORD = {
    "([d16,B],d16)", {4, 0, 14, 16, .reads = 2, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_WORD_BD_LONG = {
    "([d32,B],d16)", {4, 0, 18, 20, .reads = 2, .prefetches = 2}, true, false};

/* Memory indirect with a long outer displacement. */
static const ap_m68030_ea_timing_t FULL_OD_LONG_BD_NULL = {
    "([B],d32)", {4, 0, 12, 14, .reads = 2, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_LONG_BD_WORD_BASED = {
    "([d16,An],d32)", {2, 0, 12, 14, .reads = 2, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_LONG_BD_WORD = {
    "([d16,B],d32)", {4, 0, 14, 17, .reads = 2, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FULL_OD_LONG_BD_LONG = {
    "([d32,B],d32)", {4, 0, 18, 21, .reads = 2, .prefetches = 3}, true, false};

const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_timing_full(const ap_m68030_extension_t *extension) {
  if (extension == nullptr || !extension->full_format || extension->reserved) {
    return nullptr;
  }

  /* A word base displacement is free when the base is a register, and costs two
   * clocks when it is not -- the reading the header sets out. `base_suppressed`
   * is what separates them: with BS set there is no register to fold the
   * displacement into. */
  const bool word_based = extension->base_displacement_size == AP_M68030_BD_WORD &&
                          !extension->base_suppressed;

  switch (extension->indirect) {
  case AP_M68030_INDIRECT_NONE:
    switch (extension->base_displacement_size) {
    case AP_M68030_BD_NULL:
      return &FULL_NONE_BD_NULL;
    case AP_M68030_BD_WORD:
      return word_based ? &FULL_NONE_BD_WORD_BASED : &FULL_NONE_BD_WORD;
    case AP_M68030_BD_LONG:
      return &FULL_NONE_BD_LONG;
    case AP_M68030_BD_RESERVED:
      break;
    }
    return nullptr;

  case AP_M68030_INDIRECT_PREINDEXED:
  case AP_M68030_INDIRECT_POSTINDEXED:
  case AP_M68030_INDIRECT_MEMORY:
    /* The index makes no difference to the figures: `([B])` and `([B],I)` are
     * both `10(2/0/0)`, and the same holds at every outer displacement. So
     * preindexed, postindexed and index-suppressed share a row, and the table's
     * note that "scaling and size of Xn do not affect timing" is the same
     * statement from the other side. */
    switch (extension->outer_displacement_size) {
    case AP_M68030_OD_NULL:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &FULL_OD_NULL_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &FULL_OD_NULL_BD_WORD_BASED : &FULL_OD_NULL_BD_WORD;
      case AP_M68030_BD_LONG: return &FULL_OD_NULL_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_WORD:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &FULL_OD_WORD_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &FULL_OD_WORD_BD_WORD_BASED : &FULL_OD_WORD_BD_WORD;
      case AP_M68030_BD_LONG: return &FULL_OD_WORD_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_LONG:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &FULL_OD_LONG_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &FULL_OD_LONG_BD_WORD_BASED : &FULL_OD_LONG_BD_WORD;
      case AP_M68030_BD_LONG: return &FULL_OD_LONG_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_NONE:
      /* A memory indirect action always has an outer displacement size, even
       * when it is null. `NONE` means there is no indirection at all, which the
       * arm above handles -- so reaching here is an inconsistent decode rather
       * than a row this table lacks. */
      break;
    }
    return nullptr;

  case AP_M68030_INDIRECT_RESERVED:
    break;
  }
  return nullptr;
}


/* §11.6.3's FULL FORMAT EXTENSION WORD(S) rows, from the page image.
 *
 * The same sixteen-way space as the fetch table's, and it **confirms the
 * reading** that selects between the table's two groups on a second, entirely
 * separate table: `(d16,An)` is 6 against `(B)`'s 6, `([d16,An])` 10 against
 * `([B])`'s 10, `([d16,An],d16)` 12 against `([B],d16)`'s 12 -- every group A
 * row equal to its group B row with the base displacement dropped, exactly as
 * in §11.6.1. Sixteen rows across two tables with no counterexample.
 *
 * The head column corroborates it in a way the fetch table cannot: here the
 * group A rows carry a *plain* head of 2 while `(B)` carries "6+op head", so
 * the two groups differ in kind and not merely in value.
 *
 * Reads are one fewer than the fetch table's throughout, which is §11.6.3's own
 * sentence: "Fetch time is only included for the first level of indirection on
 * memory indirect addressing modes." A non-indirect calculate reads nothing at
 * all. */
static const ap_m68030_ea_timing_t CALC_FULL_NONE_BD_NULL = {
    "(B)", {6, 0, 6, 6, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CALC_FULL_NONE_BD_WORD_BASED = {
    "(d16,An) or (d16,PC)", {2, 0, 6, 6, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_NONE_BD_WORD = {
    "(d16,B)", {4, 0, 8, 9, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_NONE_BD_LONG = {
    "(d32,B)", {4, 0, 12, 12, .prefetches = 2}, true, false};

static const ap_m68030_ea_timing_t CALC_FULL_OD_NULL_BD_NULL = {
    "([B])", {4, 0, 10, 10, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_NULL_BD_WORD_BASED = {
    "([d16,An]) or ([d16,PC])", {2, 0, 10, 10, .reads = 1, .prefetches = 1},
    true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_NULL_BD_WORD = {
    "([d16,B])", {4, 0, 12, 13, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_NULL_BD_LONG = {
    "([d32,B])", {4, 0, 16, 17, .reads = 1, .prefetches = 2}, true, false};

static const ap_m68030_ea_timing_t CALC_FULL_OD_WORD_BD_NULL = {
    "([B],d16)", {4, 0, 12, 13, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_WORD_BD_WORD_BASED = {
    "([d16,An],d16)", {2, 0, 12, 13, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_WORD_BD_WORD = {
    "([d16,B],d16)", {4, 0, 14, 16, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_WORD_BD_LONG = {
    "([d32,B],d16)", {4, 0, 18, 20, .reads = 1, .prefetches = 2}, true, false};

static const ap_m68030_ea_timing_t CALC_FULL_OD_LONG_BD_NULL = {
    "([B],d32)", {4, 0, 12, 13, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_LONG_BD_WORD_BASED = {
    "([d16,An],d32)", {2, 0, 12, 13, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_LONG_BD_WORD = {
    "([d16,B],d32)", {4, 0, 14, 16, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t CALC_FULL_OD_LONG_BD_LONG = {
    "([d32,B],d32)", {4, 0, 18, 20, .reads = 1, .prefetches = 3}, true, false};

const ap_m68030_ea_timing_t *
ap_m68030_ea_calculate_timing_full(const ap_m68030_extension_t *extension) {
  if (extension == nullptr || !extension->full_format || extension->reserved) {
    return nullptr;
  }
  const bool word_based = extension->base_displacement_size == AP_M68030_BD_WORD &&
                          !extension->base_suppressed;

  switch (extension->indirect) {
  case AP_M68030_INDIRECT_NONE:
    switch (extension->base_displacement_size) {
    case AP_M68030_BD_NULL: return &CALC_FULL_NONE_BD_NULL;
    case AP_M68030_BD_WORD:
      return word_based ? &CALC_FULL_NONE_BD_WORD_BASED : &CALC_FULL_NONE_BD_WORD;
    case AP_M68030_BD_LONG: return &CALC_FULL_NONE_BD_LONG;
    case AP_M68030_BD_RESERVED: break;
    }
    return nullptr;

  case AP_M68030_INDIRECT_PREINDEXED:
  case AP_M68030_INDIRECT_POSTINDEXED:
  case AP_M68030_INDIRECT_MEMORY:
    switch (extension->outer_displacement_size) {
    case AP_M68030_OD_NULL:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &CALC_FULL_OD_NULL_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &CALC_FULL_OD_NULL_BD_WORD_BASED
                          : &CALC_FULL_OD_NULL_BD_WORD;
      case AP_M68030_BD_LONG: return &CALC_FULL_OD_NULL_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_WORD:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &CALC_FULL_OD_WORD_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &CALC_FULL_OD_WORD_BD_WORD_BASED
                          : &CALC_FULL_OD_WORD_BD_WORD;
      case AP_M68030_BD_LONG: return &CALC_FULL_OD_WORD_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_LONG:
      switch (extension->base_displacement_size) {
      case AP_M68030_BD_NULL: return &CALC_FULL_OD_LONG_BD_NULL;
      case AP_M68030_BD_WORD:
        return word_based ? &CALC_FULL_OD_LONG_BD_WORD_BASED
                          : &CALC_FULL_OD_LONG_BD_WORD;
      case AP_M68030_BD_LONG: return &CALC_FULL_OD_LONG_BD_LONG;
      case AP_M68030_BD_RESERVED: break;
      }
      return nullptr;
    case AP_M68030_OD_NONE:
      break;
    }
    return nullptr;

  case AP_M68030_INDIRECT_RESERVED:
    break;
  }
  return nullptr;
}

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
