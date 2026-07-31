/* MC68030 published instruction timings, `[030]` §11.6. See the header for
 * which rows are transcribed and why the rest are not. */

#include "cpu/m68030/ap_m68030_timing_table.h"

/* Every row here has an instruction-cache case of the form `n(0/0/0)` in
 * §11.6.8 or §11.6.9 -- no reads, no prefetches, no writes -- so `n` is pure
 * microcode time. The head and tail are the same tables' first two columns.
 *
 * The word-size address forms costing 4 against the long forms' 2 is the
 * pattern that appears six times across ADDA, SUBA and CMPA, and it is the
 * direction that makes physical sense: the word form sign-extends its source to
 * 32 bits before operating and the long form does not. */
static const ap_m68030_table_entry_t TABLE[] = {
    /* §11.6.8, Arithmetical/Logical Instructions. */
    {"ADD Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"ADDA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4}, false, false},
    {"ADDA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"AND Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"EOR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"OR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"SUB Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"SUBA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4}, false, false},
    {"SUBA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"CMP Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"CMPA Rn,An", {.head = 4, .tail = 0, .cache_case = 4}, false, false},

    /* The divides, marked `+` in the table: "Indicates Maximum Time (Actual
     * time is data dependent)". PROVISIONAL. */
    {"DIVS.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 56}, true, false},
    {"DIVS.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 90}, true, false},
    {"DIVU.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 44}, true, false},
    {"DIVU.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 78}, true, false},

    /* §11.6.9, Immediate Arithmetical/Logical Instructions. */
    {"MOVEQ #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"ADDQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    {"SUBQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2}, false, false},
    /* `**` in the table: the immediate is fetched through a separate effective
     * address time, so this figure is not the whole cost. */
    {"ADDI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2}, false, true},
};

#define TABLE_COUNT (sizeof TABLE / sizeof TABLE[0])

const ap_m68030_table_entry_t *ap_m68030_timing_table(unsigned *count) {
  *count = (unsigned)TABLE_COUNT;
  return TABLE;
}

/* Index into TABLE, by name, so the mapping below reads as the table does and a
 * reordering of the rows cannot silently change what an opcode maps to. */
enum {
  ROW_ADD_RN_DN = 0,
  ROW_ADDA_W,
  ROW_ADDA_L,
  ROW_AND_DN_DN,
  ROW_EOR_DN_DN,
  ROW_OR_DN_DN,
  ROW_SUB_RN_DN,
  ROW_SUBA_W,
  ROW_SUBA_L,
  ROW_CMP_RN_DN,
  ROW_CMPA_RN_AN,
  ROW_DIVS_W,
  ROW_DIVS_L,
  ROW_DIVU_W,
  ROW_DIVU_L,
  ROW_MOVEQ,
  ROW_ADDQ,
  ROW_SUBQ,
  ROW_ADDI_DN,
};

const ap_m68030_table_entry_t *ap_m68030_timing_for_word(uint16_t instruction) {
  const unsigned family = (unsigned)((instruction >> 12) & 0xFu);
  const unsigned opmode = (unsigned)((instruction >> 6) & 0x7u);
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);

  /* Only register-direct sources: those are the rows transcribed, and a memory
   * form's published figure needs an effective address time this does not
   * carry. Mode 000 is a data register, 001 an address register. */
  const bool register_source = (mode == 0x0u) || (mode == 0x1u);

  /* MOVEQ is family 0111 with bit 8 clear, and takes no operand at all. */
  if (family == 0x7u && ((instruction >> 8) & 1u) == 0u) {
    return &TABLE[ROW_MOVEQ];
  }

  if (!register_source) {
    return nullptr;
  }

  /* ADDQ and SUBQ share family 0101 with Scc and DBcc, which are told apart by
   * the size field reading 11. */
  if (family == 0x5u && opmode != 0x3u && opmode != 0x7u) {
    const bool subtract = ((instruction >> 8) & 1u) != 0u;
    return subtract ? &TABLE[ROW_SUBQ] : &TABLE[ROW_ADDQ];
  }

  /* The address-register forms: opmode 011 is the word size and 111 the long,
   * which is the distinction the whole word-costs-4 pattern rests on. */
  const bool address_form = (opmode == 0x3u) || (opmode == 0x7u);
  const bool long_size = (opmode == 0x7u);

  switch (family) {
  case 0xDu: /* ADD / ADDA */
    if (address_form) {
      return long_size ? &TABLE[ROW_ADDA_L] : &TABLE[ROW_ADDA_W];
    }
    return &TABLE[ROW_ADD_RN_DN];

  case 0x9u: /* SUB / SUBA */
    if (address_form) {
      return long_size ? &TABLE[ROW_SUBA_L] : &TABLE[ROW_SUBA_W];
    }
    return &TABLE[ROW_SUB_RN_DN];

  case 0xBu: /* CMP / CMPA, and EOR in the other direction */
    if (address_form) {
      return &TABLE[ROW_CMPA_RN_AN];
    }
    /* "CMP and EOR do not overlap": CMP takes the register direction, EOR the
     * memory one, and only EOR's register-destination form is a Dn,Dn. */
    if (opmode >= 0x4u) {
      return &TABLE[ROW_EOR_DN_DN];
    }
    return &TABLE[ROW_CMP_RN_DN];

  case 0xCu: /* AND, and the wide forms MULU/MULS which are not transcribed */
    if (address_form) {
      return nullptr;
    }
    return &TABLE[ROW_AND_DN_DN];

  case 0x8u: /* OR, and the wide forms DIVU/DIVS */
    if (address_form) {
      /* Family 1000's two wide opmodes are both *word* divides: 011 is DIVU and
       * 111 is DIVS, each dividing a long by a word, which is what
       * ap_m68030_arith_decode reports too. The `long_size` flag names the
       * opmode here rather than an operand width. The 32-bit divides are a
       * family 0100 extension and this lookup does not reach them. */
      return long_size ? &TABLE[ROW_DIVS_W] : &TABLE[ROW_DIVU_W];
    }
    return &TABLE[ROW_OR_DN_DN];

  default:
    break;
  }
  return nullptr;
}

/* A row added to TABLE without an index, or an index without a row, is a
 * mismatch this catches at compile time -- which is the failure a later
 * transcription is most likely to introduce.
 *
 * It does *not* claim every row is reachable from ap_m68030_timing_for_word,
 * and three are not: the 32-bit divides are a family 0100 extension the lookup
 * does not decode, and ADDI's immediate arrives through an effective address
 * time this module does not carry. They are transcribed because they were read,
 * and reachable through ap_m68030_timing_table() for anything that wants the
 * published figure directly. */
static_assert(TABLE_COUNT == (unsigned)ROW_ADDI_DN + 1u,
              "every transcribed row must have an index, and vice versa");
