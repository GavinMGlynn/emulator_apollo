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

/* §11.6.5, Jump Effective Address, from the page image (p. 11-35): the address
 * `JMP` or `JSR` goes to. Every row is `n(0/0/0)` in **both** columns -- no
 * operand read, and no prefetch of its own, because the refill at the target
 * belongs to the operation's row. Every head is written "+op head".
 *
 * Only the control modes have rows. A jump through `(An)+`, `-(An)`, a register
 * or an immediate is not an instruction, and the table prints nothing for them.
 *
 * The single-address block names `(d16,An)` alone, where §11.6.1, §11.6.3 and
 * this table's own brief and full-format blocks write "or (d16,PC)" with one
 * figure for both. `(d16,PC)` takes the `(d16,An)` row, the reading §11.6.2's
 * transcription already makes for the same omission. The `MC68020 User's
 * Manual` §9.2.5 omits it identically, and its cache-case column agrees with
 * every figure here: 2, 4, 2, 2 and 6. */
static const ap_m68030_ea_timing_t JUMP_INDIRECT = {
    "(An)", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t JUMP_DISPLACEMENT = {
    "(d16,An)", {4, 0, 4, 4, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t JUMP_ABSOLUTE_SHORT = {
    "(xxx).W", {2, 0, 2, 2, .prefetches = 0}, true, true};
/* No dearer than the short form, unlike every other table: the address is
 * already in the pipe and nothing has to be sign-extended. */
static const ap_m68030_ea_timing_t JUMP_ABSOLUTE_LONG = {
    "(xxx).L", {2, 0, 2, 2, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t JUMP_INDEXED = {
    "(d8,An,Xn) or (d8,PC,Xn)", {6, 0, 6, 6, .prefetches = 0}, true, true};

/* §11.6.4, Calculate Immediate Effective Address, from the page image
 * (p. 11-33): "the number of clock periods needed for the processor to fetch the
 * immediate source operand and calculate the specified destination effective
 * address", and for a two-word instruction "to fetch the second word of the
 * instruction and calculate the specified source operand or single operand".
 * The second reading is every consumer this core has, so the word column is the
 * one used; the long column is here because the page prints it.
 *
 * Nothing is read in either column, and the no-cache `p` is the extension
 * word's own fetch. `(An)+` is the one plain head, as in §11.6.3. `(d16,PC)`
 * shares `(d16,An)`'s row, the reading §11.6.2 and §11.6.5 make for the same
 * omission. */
static const ap_m68030_ea_timing_t CIEA_W_DN = {
    "#(data).W,Dn", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_DN = {
    "#(data).L,Dn", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_INDIRECT = {
    "#(data).W,(An)", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_INDIRECT = {
    "#(data).L,(An)", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_POSTINCREMENT = {
    "#(data).W,(An)+", {2, 0, 4, 4, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CIEA_L_POSTINCREMENT = {
    "#(data).L,(An)+", {4, 0, 6, 6, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t CIEA_W_PREDECREMENT = {
    "#(data).W,-(An)", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_PREDECREMENT = {
    "#(data).L,-(An)", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_DISPLACEMENT = {
    "#(data).W,(d16,An)", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_DISPLACEMENT = {
    "#(data).L,(d16,An)", {6, 0, 6, 7, .prefetches = 2}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_ABSOLUTE_SHORT = {
    "#(data).W,$XXX.W", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_ABSOLUTE_SHORT = {
    "#(data).L,$XXX.W", {6, 0, 6, 6, .prefetches = 2}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_ABSOLUTE_LONG = {
    "#(data).W,$XXX.L", {6, 0, 6, 6, .prefetches = 2}, true, true};
static const ap_m68030_ea_timing_t CIEA_L_ABSOLUTE_LONG = {
    "#(data).L,$XXX.L", {8, 0, 8, 8, .prefetches = 2}, true, true};
static const ap_m68030_ea_timing_t CIEA_W_INDEXED = {
    "#(data).W,(d8,An,Xn) or (d8,PC,Xn)", {6, 0, 6, 6, .prefetches = 2}, true,
    true};
static const ap_m68030_ea_timing_t CIEA_L_INDEXED = {
    "#(data).L,(d8,An,Xn) or (d8,PC,Xn)", {8, 0, 8, 8, .prefetches = 2}, true,
    true};

const ap_m68030_ea_timing_t *
ap_m68030_ea_calculate_immediate_timing(ap_m68030_ea_kind_t kind,
                                        bool immediate_long) {
  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
    return immediate_long ? &CIEA_L_DN : &CIEA_W_DN;
  case AP_M68030_EA_ADDRESS_INDIRECT:
    return immediate_long ? &CIEA_L_INDIRECT : &CIEA_W_INDIRECT;
  case AP_M68030_EA_POSTINCREMENT:
    return immediate_long ? &CIEA_L_POSTINCREMENT : &CIEA_W_POSTINCREMENT;
  case AP_M68030_EA_PREDECREMENT:
    return immediate_long ? &CIEA_L_PREDECREMENT : &CIEA_W_PREDECREMENT;
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT:
    return immediate_long ? &CIEA_L_DISPLACEMENT : &CIEA_W_DISPLACEMENT;
  case AP_M68030_EA_ABSOLUTE_SHORT:
    return immediate_long ? &CIEA_L_ABSOLUTE_SHORT : &CIEA_W_ABSOLUTE_SHORT;
  case AP_M68030_EA_ABSOLUTE_LONG:
    return immediate_long ? &CIEA_L_ABSOLUTE_LONG : &CIEA_W_ABSOLUTE_LONG;
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    /* The brief format only, as in the other tables. */
    return immediate_long ? &CIEA_L_INDEXED : &CIEA_W_INDEXED;
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    break;
  }
  return nullptr;
}

/* Which of a full-format table's seventeen figure sets an extension word
 * selects: group A -- a word base displacement off a register, the reading
 * `ap_m68030_ea_fetch_timing_full` sets out -- by indirection and outer size,
 * its non-indirect row split by the index; group B by base size against
 * indirection and outer size. -1 for a word no row can take. */
enum {
  FULL_A_NONE,
  FULL_A_NONE_INDEXED,
  FULL_A_OD_NULL,
  FULL_A_OD_WORD,
  FULL_A_OD_LONG,
  FULL_B_NONE_BD_NULL,
  FULL_B_NONE_BD_WORD,
  FULL_B_NONE_BD_LONG,
  FULL_B_OD_NULL_BD_NULL,
  FULL_B_OD_NULL_BD_WORD,
  FULL_B_OD_NULL_BD_LONG,
  FULL_B_OD_WORD_BD_NULL,
  FULL_B_OD_WORD_BD_WORD,
  FULL_B_OD_WORD_BD_LONG,
  FULL_B_OD_LONG_BD_NULL,
  FULL_B_OD_LONG_BD_WORD,
  FULL_B_OD_LONG_BD_LONG,
  FULL_SLOTS,
};

static int full_slot(const ap_m68030_extension_t *e) {
  if (e == nullptr || !e->full_format || e->reserved ||
      e->base_displacement_size == AP_M68030_BD_RESERVED ||
      e->indirect == AP_M68030_INDIRECT_RESERVED) {
    return -1;
  }
  const bool word_based =
      e->base_displacement_size == AP_M68030_BD_WORD && !e->base_suppressed;
  const int bd = e->base_displacement_size == AP_M68030_BD_NULL   ? 0
                 : e->base_displacement_size == AP_M68030_BD_WORD ? 1
                                                                  : 2;
  if (e->indirect == AP_M68030_INDIRECT_NONE) {
    if (word_based) {
      return e->index_suppressed ? FULL_A_NONE : FULL_A_NONE_INDEXED;
    }
    return FULL_B_NONE_BD_NULL + bd;
  }
  int od;
  switch (e->outer_displacement_size) {
  case AP_M68030_OD_NULL: od = 0; break;
  case AP_M68030_OD_WORD: od = 1; break;
  case AP_M68030_OD_LONG: od = 2; break;
  case AP_M68030_OD_NONE:
  default:
    return -1;
  }
  if (word_based) {
    return FULL_A_OD_NULL + od;
  }
  return FULL_B_OD_NULL_BD_NULL + 3 * od + bd;
}

/* §11.6.2's FULL FORMAT EXTENSION WORD(S) rows, from the page images
 * (pp. 11-29, 11-30), a word and a long immediate apiece. Reads are the
 * destination's and, behind an indirection, its first level's. The index shows
 * in the non-indirect group A head, 4 against 6 for a word immediate, as it does
 * in §11.6.5's; every other row is the same with or without it. Group A equals
 * group B less its base displacement on this table too. */
static const ap_m68030_ea_timing_t FIEA_FULL_W[FULL_SLOTS] = {
    [FULL_A_NONE] = {"#<data>.W,(d16,An) or (d16,PC)", {4, 0, 8, 9, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_NONE_INDEXED] = {"#<data>.W,(d16,An,Xn) or (d16,PC,Xn)", {6, 0, 8, 9, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_OD_NULL] = {"#<data>.W,([d16,An]) or ([d16,PC])", {4, 0, 12, 12, .reads = 2, .prefetches = 2}, true, false},
    [FULL_A_OD_WORD] = {"#<data>.W,([d16,An],d16) or ([d16,PC],d16)", {4, 0, 14, 15, .reads = 2, .prefetches = 2}, true, false},
    [FULL_A_OD_LONG] = {"#<data>.W,([d16,An],d32) or ([d16,PC],d32)", {4, 0, 14, 16, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_NONE_BD_NULL] = {"#<data>.W,(B)", {6, 0, 8, 9, .reads = 1, .prefetches = 1}, true, false},
    [FULL_B_NONE_BD_WORD] = {"#<data>.W,(d16,B)", {6, 0, 10, 12, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_NONE_BD_LONG] = {"#<data>.W,(d32,B)", {10, 0, 14, 16, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_NULL] = {"#<data>.W,([B])", {6, 0, 12, 12, .reads = 2, .prefetches = 1}, true, false},
    [FULL_B_OD_NULL_BD_WORD] = {"#<data>.W,([d16,B])", {6, 0, 14, 15, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_LONG] = {"#<data>.W,([d32,B])", {6, 0, 18, 19, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_NULL] = {"#<data>.W,([B],d16)", {6, 0, 14, 15, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_WORD] = {"#<data>.W,([d16,B],d16)", {6, 0, 16, 18, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_LONG] = {"#<data>.W,([d32,B],d16)", {6, 0, 20, 22, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_NULL] = {"#<data>.W,([B],d32)", {6, 0, 14, 16, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_LONG_BD_WORD] = {"#<data>.W,([d16,B],d32)", {6, 0, 16, 19, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_LONG] = {"#<data>.W,([d32,B],d32)", {6, 0, 20, 23, .reads = 2, .prefetches = 3}, true, false},
};
static const ap_m68030_ea_timing_t FIEA_FULL_L[FULL_SLOTS] = {
    [FULL_A_NONE] = {"#<data>.L,(d16,An) or (d16,PC)", {6, 0, 10, 11, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_NONE_INDEXED] = {"#<data>.L,(d16,An,Xn) or (d16,PC,Xn)", {8, 0, 10, 11, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_OD_NULL] = {"#<data>.L,([d16,An]) or ([d16,PC])", {6, 0, 14, 14, .reads = 2, .prefetches = 2}, true, false},
    [FULL_A_OD_WORD] = {"#<data>.L,([d16,An],d16) or ([d16,PC],d16)", {6, 0, 16, 17, .reads = 2, .prefetches = 3}, true, false},
    [FULL_A_OD_LONG] = {"#<data>.L,([d16,An],d32) or ([d16,PC],d32)", {6, 0, 16, 18, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_NONE_BD_NULL] = {"#<data>.L,(B)", {8, 0, 10, 11, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_NONE_BD_WORD] = {"#<data>.L,(d16,B)", {8, 0, 12, 14, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_NONE_BD_LONG] = {"#<data>.L,(d32,B)", {12, 0, 16, 18, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_NULL_BD_NULL] = {"#<data>.L,([B])", {8, 0, 14, 14, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_WORD] = {"#<data>.L,([d16,B])", {8, 0, 16, 17, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_LONG] = {"#<data>.L,([d32,B])", {8, 0, 20, 21, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_WORD_BD_NULL] = {"#<data>.L,([B],d16)", {8, 0, 16, 17, .reads = 2, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_WORD] = {"#<data>.L,([d16,B],d16)", {8, 0, 18, 20, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_WORD_BD_LONG] = {"#<data>.L,([d32,B],d16)", {8, 0, 22, 24, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_NULL] = {"#<data>.L,([B],d32)", {8, 0, 16, 18, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_WORD] = {"#<data>.L,([d16,B],d32)", {8, 0, 18, 21, .reads = 2, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_LONG] = {"#<data>.L,([d32,B],d32)", {8, 0, 22, 25, .reads = 2, .prefetches = 4}, true, false},
};

/* §11.6.4's FULL FORMAT EXTENSION WORD(S) rows, from the page images
 * (pp. 11-33 to 11-35). Nothing is read but the first level of indirection. The
 * index shows in the non-indirect group A head again, "8+op" against 4, and
 * the `(B)` rows' heads are "+op" too.
 *
 * **Two figures are readings.** `#(data).W,([d16,An],d32)` prints its no-cache
 * case as `16(1/3/0)`, where the same row with an index prints 15 and group B's
 * `([B],d32)` -- which group A equals everywhere else -- prints 15: taken as 15.
 * And `#(data).L,([B],I,d16)` prints `16(2/0/0)`, two reads where every row
 * about it has one; the same slip §11.6.3's `([B],I,d32)` makes: taken as one.
 * Both recorded in `M68030_WALK.md`. */
static const ap_m68030_ea_timing_t CIEA_FULL_W[FULL_SLOTS] = {
    [FULL_A_NONE] = {"#(data).W,(d16,An) or (d16,PC)", {4, 0, 8, 8, .prefetches = 2}, true, false},
    [FULL_A_NONE_INDEXED] = {"#(data).W,(d16,An,Xn) or (d16,PC,Xn)", {8, 0, 8, 8, .prefetches = 2}, true, true},
    [FULL_A_OD_NULL] = {"#(data).W,([d16,An]) or ([d16,PC])", {4, 0, 12, 12, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_OD_WORD] = {"#(data).W,([d16,An],d16) or ([d16,PC],d16)", {4, 0, 14, 15, .reads = 1, .prefetches = 2}, true, false},
    [FULL_A_OD_LONG] = {"#(data).W,([d16,An],d32) or ([d16,PC],d32)", {4, 0, 14, 15, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_NONE_BD_NULL] = {"#(data).W,(B)", {8, 0, 8, 8, .prefetches = 1}, true, true},
    [FULL_B_NONE_BD_WORD] = {"#(data).W,(d16,B)", {6, 0, 10, 11, .prefetches = 2}, true, false},
    [FULL_B_NONE_BD_LONG] = {"#(data).W,(d32,B)", {6, 0, 14, 15, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_NULL] = {"#(data).W,([B])", {6, 0, 12, 12, .reads = 1, .prefetches = 1}, true, false},
    [FULL_B_OD_NULL_BD_WORD] = {"#(data).W,([d16,B])", {6, 0, 14, 15, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_LONG] = {"#(data).W,([d32,B])", {6, 0, 18, 19, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_NULL] = {"#(data).W,([B],d16)", {6, 0, 14, 15, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_WORD] = {"#(data).W,([d16,B],d16)", {6, 0, 16, 18, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_LONG] = {"#(data).W,([d32,B],d16)", {6, 0, 20, 22, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_NULL] = {"#(data).W,([B],d32)", {6, 0, 14, 15, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_LONG_BD_WORD] = {"#(data).W,([d16,B],d32)", {6, 0, 16, 18, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_LONG] = {"#(data).W,([d32,B],d32)", {6, 0, 20, 22, .reads = 1, .prefetches = 3}, true, false},
};
static const ap_m68030_ea_timing_t CIEA_FULL_L[FULL_SLOTS] = {
    [FULL_A_NONE] = {"#(data).L,(d16,An) or (d16,PC)", {6, 0, 10, 10, .prefetches = 2}, true, false},
    [FULL_A_NONE_INDEXED] = {"#(data).L,(d16,An,Xn) or (d16,PC,Xn)", {10, 0, 10, 10, .prefetches = 2}, true, true},
    [FULL_A_OD_NULL] = {"#(data).L,([d16,An]) or ([d16,PC])", {6, 0, 14, 14, .reads = 1, .prefetches = 1}, true, false},
    [FULL_A_OD_WORD] = {"#(data).L,([d16,An],d16) or ([d16,PC],d16)", {6, 0, 16, 17, .reads = 1, .prefetches = 3}, true, false},
    [FULL_A_OD_LONG] = {"#(data).L,([d16,An],d32) or ([d16,PC],d32)", {6, 0, 16, 17, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_NONE_BD_NULL] = {"#(data).L,(B)", {10, 0, 10, 10, .prefetches = 2}, true, true},
    [FULL_B_NONE_BD_WORD] = {"#(data).L,(d16,B)", {8, 0, 12, 13, .prefetches = 2}, true, false},
    [FULL_B_NONE_BD_LONG] = {"#(data).L,(d32,B)", {8, 0, 16, 17, .prefetches = 3}, true, false},
    [FULL_B_OD_NULL_BD_NULL] = {"#(data).L,([B])", {8, 0, 14, 14, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_WORD] = {"#(data).L,([d16,B])", {8, 0, 16, 17, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_NULL_BD_LONG] = {"#(data).L,([d32,B])", {8, 0, 20, 21, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_WORD_BD_NULL] = {"#(data).L,([B],d16)", {8, 0, 16, 17, .reads = 1, .prefetches = 2}, true, false},
    [FULL_B_OD_WORD_BD_WORD] = {"#(data).L,([d16,B],d16)", {8, 0, 18, 20, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_WORD_BD_LONG] = {"#(data).L,([d32,B],d16)", {8, 0, 22, 24, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_NULL] = {"#(data).L,([B],d32)", {8, 0, 16, 17, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_WORD] = {"#(data).L,([d16,B],d32)", {8, 0, 18, 20, .reads = 1, .prefetches = 3}, true, false},
    [FULL_B_OD_LONG_BD_LONG] = {"#(data).L,([d32,B],d32)", {8, 0, 22, 24, .reads = 1, .prefetches = 4}, true, false},
};

const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_immediate_timing_full(const ap_m68030_extension_t *extension,
                                         bool immediate_long) {
  const int slot = full_slot(extension);
  if (slot < 0) {
    return nullptr;
  }
  return immediate_long ? &FIEA_FULL_L[slot] : &FIEA_FULL_W[slot];
}

const ap_m68030_ea_timing_t *ap_m68030_ea_calculate_immediate_timing_full(
    const ap_m68030_extension_t *extension, bool immediate_long) {
  const int slot = full_slot(extension);
  if (slot < 0) {
    return nullptr;
  }
  return immediate_long ? &CIEA_FULL_L[slot] : &CIEA_FULL_W[slot];
}

/* §11.6.5's FULL FORMAT EXTENSION WORD(S) rows, from the page image
 * (p. 11-36). The same two groups as §11.6.1's, and the same reading chooses
 * between them: every group A row equals its group B row with the base
 * displacement dropped.
 *
 * Two things differ from the fetch table. **The index shows in the head of the
 * non-indirect group A row** -- `(d16,An)` has a head of 2 where `(d16,An,Xn)`
 * has "6+op head", on the same 6 clocks -- so that row splits on the index
 * suppress bit. And **a word and a long outer displacement cost the same**, 12
 * behind `([d16,An])` and `([B])`, 14 behind `([d16,B])`, 18 behind
 * `([d32,B])`. Reads are one throughout, the first level of indirection's.
 *
 * The page lists `([B],d32)` twice with identical figures; `M68030_WALK.md`
 * records it. */
static const ap_m68030_ea_timing_t JUMP_FULL_A_NONE = {
    "(d16,An) or (d16,PC)", {2, 0, 6, 6, .prefetches = 0}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_A_NONE_INDEXED = {
    "(d16,An,Xn) or (d16,PC,Xn)", {6, 0, 6, 6, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t JUMP_FULL_A_OD_NULL = {
    "([d16,An]) or ([d16,PC])", {2, 0, 10, 10, .reads = 1, .prefetches = 1},
    true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_A_OD = {
    "([d16,An],d16) or ([d16,PC],d16)", {2, 0, 12, 12, .reads = 1, .prefetches = 1},
    true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_NONE_BD_NULL = {
    "(B)", {6, 0, 6, 6, .prefetches = 0}, true, true};
static const ap_m68030_ea_timing_t JUMP_FULL_NONE_BD_WORD = {
    "(d16,B)", {4, 0, 8, 9, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_NONE_BD_LONG = {
    "(d32,B)", {4, 0, 12, 13, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_NULL_BD_NULL = {
    "([B])", {4, 0, 10, 10, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_BD_NULL = {
    "([B],d16)", {4, 0, 12, 12, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_NULL_BD_WORD = {
    "([d16,B])", {4, 0, 12, 13, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_BD_WORD = {
    "([d16,B],d16)", {4, 0, 14, 15, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_NULL_BD_LONG = {
    "([d32,B])", {4, 0, 16, 17, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t JUMP_FULL_OD_BD_LONG = {
    "([d32,B],d16)", {4, 0, 18, 19, .reads = 1, .prefetches = 2}, true, false};

const ap_m68030_ea_timing_t *
ap_m68030_ea_jump_timing_full(const ap_m68030_extension_t *extension) {
  if (extension == nullptr || !extension->full_format || extension->reserved) {
    return nullptr;
  }
  const bool word_based = extension->base_displacement_size == AP_M68030_BD_WORD &&
                          !extension->base_suppressed;
  if (extension->base_displacement_size == AP_M68030_BD_RESERVED ||
      extension->indirect == AP_M68030_INDIRECT_RESERVED) {
    return nullptr;
  }

  if (extension->indirect == AP_M68030_INDIRECT_NONE) {
    if (word_based) {
      return extension->index_suppressed ? &JUMP_FULL_A_NONE
                                         : &JUMP_FULL_A_NONE_INDEXED;
    }
    switch (extension->base_displacement_size) {
    case AP_M68030_BD_NULL: return &JUMP_FULL_NONE_BD_NULL;
    case AP_M68030_BD_WORD: return &JUMP_FULL_NONE_BD_WORD;
    case AP_M68030_BD_LONG: return &JUMP_FULL_NONE_BD_LONG;
    case AP_M68030_BD_RESERVED: break;
    }
    return nullptr;
  }

  if (extension->outer_displacement_size == AP_M68030_OD_NONE) {
    return nullptr; /* an indirect action always names an outer size */
  }
  const bool outer = extension->outer_displacement_size != AP_M68030_OD_NULL;
  if (word_based) {
    return outer ? &JUMP_FULL_A_OD : &JUMP_FULL_A_OD_NULL;
  }
  switch (extension->base_displacement_size) {
  case AP_M68030_BD_NULL:
    return outer ? &JUMP_FULL_OD_BD_NULL : &JUMP_FULL_OD_NULL_BD_NULL;
  case AP_M68030_BD_WORD:
    return outer ? &JUMP_FULL_OD_BD_WORD : &JUMP_FULL_OD_NULL_BD_WORD;
  case AP_M68030_BD_LONG:
    return outer ? &JUMP_FULL_OD_BD_LONG : &JUMP_FULL_OD_NULL_BD_LONG;
  case AP_M68030_BD_RESERVED:
    break;
  }
  return nullptr;
}

const ap_m68030_ea_timing_t *ap_m68030_ea_jump_timing(ap_m68030_ea_kind_t kind) {
  switch (kind) {
  case AP_M68030_EA_ADDRESS_INDIRECT:
    return &JUMP_INDIRECT;
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT:
    return &JUMP_DISPLACEMENT;
  case AP_M68030_EA_ABSOLUTE_SHORT:
    return &JUMP_ABSOLUTE_SHORT;
  case AP_M68030_EA_ABSOLUTE_LONG:
    return &JUMP_ABSOLUTE_LONG;
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    /* The brief format only, as in the other tables: the full-format rows of
     * p. 11-36 are selected by the extension word. */
    return &JUMP_INDEXED;
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    break;
  }
  return nullptr;
}


/* §11.6.2, Fetch Immediate Effective Address, from the page image.
 *
 * The table the `**` footnote names, and it is keyed differently from the other
 * two: by the **immediate's size and the destination mode together**, because
 * it "indicates the number of clock periods needed for the processor to fetch
 * the immediate source operand *and* to calculate and fetch the specified
 * destination operand". One table entry covers both halves, which is why a `**`
 * row cannot be priced off §11.6.1 -- that table knows nothing about the
 * immediate.
 *
 * The word and long columns are genuinely different figures and not a factor:
 * `#<data>.W,(An)` is 3 and `#<data>.L,(An)` is 4, but `#<data>.W,(An)+` is 5
 * against 7. So the long immediate costs one clock more in one mode and two in
 * another, and a model scaling by operand size would be wrong in both.
 *
 * The `%` rows carry the table's own footnote, "Total head for fetch immediate
 * effective address timing includes the head time for the operation" -- the
 * same relative-head notation §11.6.3 uses. */
static const ap_m68030_ea_timing_t FIEA_W_DN = {
    "#<data>.W,Dn", {2, 0, 2, 2, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t FIEA_L_DN = {
    "#<data>.L,Dn", {4, 0, 4, 4, .prefetches = 1}, true, true};
static const ap_m68030_ea_timing_t FIEA_W_INDIRECT = {
    "#<data>.W,(An)", {1, 1, 3, 4, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_INDIRECT = {
    "#<data>.L,(An)", {1, 0, 4, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_POSTINCREMENT = {
    "#<data>.W,(An)+", {2, 1, 5, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_POSTINCREMENT = {
    "#<data>.L,(An)+", {4, 1, 7, 7, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_PREDECREMENT = {
    "#<data>.W,-(An)", {2, 2, 4, 4, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_PREDECREMENT = {
    "#<data>.L,-(An)", {2, 0, 4, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_DISPLACEMENT = {
    "#<data>.W,(d16,An)", {2, 0, 4, 5, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_DISPLACEMENT = {
    "#<data>.L,(d16,An)", {4, 0, 6, 8, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_ABSOLUTE_SHORT = {
    "#<data>.W,$XXX.W", {4, 2, 6, 6, .reads = 1, .prefetches = 1}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_ABSOLUTE_SHORT = {
    "#<data>.L,$XXX.W", {6, 2, 8, 8, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_ABSOLUTE_LONG = {
    "#<data>.W,$XXX.L", {3, 0, 6, 7, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_ABSOLUTE_LONG = {
    "#<data>.L,$XXX.L", {5, 0, 8, 9, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FIEA_W_INDEXED = {
    "#<data>.W,(d8,An,Xn) or (d8,PC,Xn)",
    {6, 2, 8, 8, .reads = 1, .prefetches = 2}, true, false};
static const ap_m68030_ea_timing_t FIEA_L_INDEXED = {
    "#<data>.L,(d8,An,Xn) or (d8,PC,Xn)",
    {8, 2, 10, 10, .reads = 1, .prefetches = 2}, true, false};
/* The one row whose *destination* is itself an immediate, which is what a
 * `CMPI #<data>,#<data>` would be. Its cache case reads nothing, since both
 * operands are in the instruction stream. */
static const ap_m68030_ea_timing_t FIEA_W_IMMEDIATE = {
    "#<data>.W,#<data>.L", {6, 0, 6, 6, .prefetches = 2}, true, true};

const ap_m68030_ea_timing_t *
ap_m68030_ea_fetch_immediate_timing(ap_m68030_ea_kind_t destination,
                                    bool immediate_long) {
  switch (destination) {
  case AP_M68030_EA_DATA_REGISTER:
    return immediate_long ? &FIEA_L_DN : &FIEA_W_DN;
  case AP_M68030_EA_ADDRESS_INDIRECT:
    return immediate_long ? &FIEA_L_INDIRECT : &FIEA_W_INDIRECT;
  case AP_M68030_EA_POSTINCREMENT:
    return immediate_long ? &FIEA_L_POSTINCREMENT : &FIEA_W_POSTINCREMENT;
  case AP_M68030_EA_PREDECREMENT:
    return immediate_long ? &FIEA_L_PREDECREMENT : &FIEA_W_PREDECREMENT;
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_PC_DISPLACEMENT:
    return immediate_long ? &FIEA_L_DISPLACEMENT : &FIEA_W_DISPLACEMENT;
  case AP_M68030_EA_ABSOLUTE_SHORT:
    return immediate_long ? &FIEA_L_ABSOLUTE_SHORT : &FIEA_W_ABSOLUTE_SHORT;
  case AP_M68030_EA_ABSOLUTE_LONG:
    return immediate_long ? &FIEA_L_ABSOLUTE_LONG : &FIEA_W_ABSOLUTE_LONG;
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED:
    /* The brief format only, as in §11.6.1: the full-format rows are their own
     * pass and returning a brief figure for one would under-count. */
    return immediate_long ? &FIEA_L_INDEXED : &FIEA_W_INDEXED;
  case AP_M68030_EA_IMMEDIATE:
    /* The table's one immediate-destination row, and it has a word-source form
     * only -- so a long immediate into an immediate has no published figure and
     * is reported absent rather than given the word row's. */
    return immediate_long ? nullptr : &FIEA_W_IMMEDIATE;
  case AP_M68030_EA_ADDRESS_REGISTER:
    /* §11.6.2 has no `An` destination row at all. The instructions it serves --
     * the `xxxI` immediate forms -- cannot take one, so this is the table
     * agreeing with the opcode map rather than a gap. */
  case AP_M68030_EA_INVALID:
    break;
  }
  return nullptr;
}

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

ap_m68030_timing_t
ap_m68030_ea_timing_composed(const ap_m68030_ea_timing_t *ea,
                             const ap_m68030_timing_t *operation) {
  ap_m68030_overlap_state_t state = ap_m68030_overlap_begin();
  ap_m68030_ea_timing_compose(&state, ea, operation);

  ap_m68030_timing_t out = *operation;
  out.cache_case = (unsigned)ap_m68030_overlap_total(&state);
  if (ea != nullptr && ea->head_applies) {
    out.head = ap_m68030_ea_timing_head(ea, operation->head);
    out.no_cache_case += ea->timing.no_cache_case;
    out.reads += ea->timing.reads;
    out.writes += ea->timing.writes;
    out.prefetches += ea->timing.prefetches;
  }
  return out;
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
