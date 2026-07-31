/* MC68030 published instruction timings, `[030]` §11.6. See the header for
 * which rows are transcribed and why the rest are not. */

#include "cpu/m68030/ap_m68030_timing_table.h"

/* Every row here has an instruction-cache case of the form `n(0/0/0)` in
 * §11.6.8 or §11.6.9 -- no reads, no prefetches, no writes -- so `n` is pure
 * microcode time. The head and tail are the same tables' first two columns.
 *
 * Every one of these rows also has `NCC` equal to `CC`, which is not a
 * coincidence and is worth stating: their no-cache case is `n(0/1/0)`, one
 * prefetch bus cycle worth two clocks, and every `n` here is at least two. The
 * fetch therefore hides entirely under the microcode and the totals coincide --
 * `max(n, 2) = n`. A row where the two differ would be one whose microcode is
 * shorter than a bus cycle, and there is none among the register forms.
 *
 * The word-size address forms costing 4 against the long forms' 2 is the
 * pattern that appears six times across ADDA, SUBA and CMPA, and it is the
 * direction that makes physical sense: the word form sign-extends its source to
 * 32 bits before operating and the long form does not. */
/* The row index *is* the table's order: every entry below is written with a
 * designated initialiser, so a row and its index cannot drift apart however the
 * file is edited.
 *
 * They did drift apart, which is why this is now written this way. Five rows
 * were inserted into the array at one point and five names into the enum at
 * another, the counts still matched, and the `static_assert` below -- which only
 * compares counts -- passed. The lookup then returned `DIVS.W`'s 56 clocks for
 * `ADD.B D0,(A0)`. A guard that checks a count catches an omission and not a
 * misordering. */
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
  ROW_ADD_DN_EA,
  ROW_SUB_DN_EA,
  ROW_AND_DN_EA,
  ROW_OR_DN_EA,
  ROW_EOR_DN_EA,
  ROW_MOVEQ,
  ROW_ADDQ,
  ROW_SUBQ,
  ROW_ADDI_DN,
  ROW_COUNT,
};

static const ap_m68030_table_entry_t TABLE[ROW_COUNT] = {
    /* §11.6.8, Arithmetical/Logical Instructions. */
    [ROW_ADD_RN_DN] = {"ADD Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_ADDA_W] = {"ADDA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4}, false, false},
    [ROW_ADDA_L] = {"ADDA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_AND_DN_DN] = {"AND Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_EOR_DN_DN] = {"EOR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_OR_DN_DN] = {"OR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_SUB_RN_DN] = {"SUB Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_SUBA_W] = {"SUBA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4}, false, false},
    [ROW_SUBA_L] = {"SUBA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_CMP_RN_DN] = {"CMP Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_CMPA_RN_AN] = {"CMPA Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4}, false, false},

    /* The divides, marked `+` in the table: "Indicates Maximum Time (Actual
     * time is data dependent)". PROVISIONAL. */
    [ROW_DIVS_W] = {"DIVS.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 56, .no_cache_case = 56}, true, false},
    [ROW_DIVS_L] = {"DIVS.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 90, .no_cache_case = 90}, true, false},
    [ROW_DIVU_W] = {"DIVU.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 44, .no_cache_case = 44}, true, false},
    [ROW_DIVU_L] = {"DIVU.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 78, .no_cache_case = 78}, true, false},

    /* The memory-destination forms. These are the first rows whose `NCC`
     * exceeds their `CC`: `3(0/0/1)` against `4(0/1/1)`, so the write hides
     * under three clocks of microcode but the write *plus* a prefetch does not.
     * Under `max(microcode, bus)` the microcode is `CC` here as elsewhere --
     * max(3,2) = 3 and max(3,4) = 4 -- and the core's own bus time supplies the
     * rest. They are what exercises the model where the register forms cannot. */
    [ROW_ADD_DN_EA] = {"ADD Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4},
     false, true},
    [ROW_SUB_DN_EA] = {"SUB Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4},
     false, true},
    [ROW_AND_DN_EA] = {"AND Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4},
     false, true},
    [ROW_OR_DN_EA] = {"OR Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4},
     false, true},
    [ROW_EOR_DN_EA] = {"EOR Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4},
     false, true},

    /* §11.6.9, Immediate Arithmetical/Logical Instructions. */
    [ROW_MOVEQ] = {"MOVEQ #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_ADDQ] = {"ADDQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    [ROW_SUBQ] = {"SUBQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, false},
    /* `**` in the table: the immediate is fetched through a separate effective
     * address time, so this figure is not the whole cost. */
    [ROW_ADDI_DN] = {"ADDI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2}, false, true},
};

#define TABLE_COUNT (sizeof TABLE / sizeof TABLE[0])

const ap_m68030_table_entry_t *ap_m68030_timing_table(unsigned *count) {
  *count = (unsigned)TABLE_COUNT;
  return TABLE;
}


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

  /* The memory-destination direction: opmodes 100-110 write the result to the
   * effective address. Those rows are transcribed for a memory destination
   * only -- a register destination in that direction is a different
   * instruction entirely (ADDX, ABCD, CMPM, EXG), which §11.6.8 lists
   * separately and this does not cover. */
  const bool to_memory = (opmode >= 0x4u) && (opmode <= 0x6u);
  if (to_memory && !register_source) {
    switch (family) {
    case 0xDu:
      return &TABLE[ROW_ADD_DN_EA];
    case 0x9u:
      return &TABLE[ROW_SUB_DN_EA];
    case 0xCu:
      return &TABLE[ROW_AND_DN_EA];
    case 0x8u:
      return &TABLE[ROW_OR_DN_EA];
    case 0xBu:
      return &TABLE[ROW_EOR_DN_EA];
    default:
      return nullptr;
    }
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
static_assert(TABLE_COUNT == (unsigned)ROW_COUNT,
              "every transcribed row must have an index, and vice versa");
