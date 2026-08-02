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
  ROW_MOVE_RN_DN,
  ROW_MOVE_RN_AN,
  ROW_MOVE_RN_IND,
  ROW_MOVE_RN_POSTINC,
  ROW_MOVE_RN_PREDEC,
  ROW_CLR_DN,
  ROW_NEG_DN,
  ROW_NEGX_DN,
  ROW_NOT_DN,
  ROW_EXT_DN,
  ROW_TST_DN,
  ROW_SCC_DN,
  ROW_TAS_DN,
  ROW_NBCD_DN,
  ROW_LS_IMM,
  ROW_ASL_IMM,
  ROW_ASR_IMM,
  ROW_RO_IMM,
  ROW_ROX_DN,
  ROW_NOP,
  ROW_RTS,
  ROW_RTR,
  ROW_RTD,
  ROW_UNLK,
  ROW_LINK_W,
  ROW_LINK_L,
  ROW_LOGICAL_TO_SR,
  ROW_BCC_TAKEN,
  ROW_BCC_B_NOT_TAKEN,
  ROW_BCC_W_NOT_TAKEN,
  ROW_BCC_L_NOT_TAKEN,
  ROW_BSR,
  ROW_DBCC_LOOPING,
  ROW_DBCC_EXPIRED,
  ROW_DBCC_TRUE,
  ROW_MOVEQ,
  ROW_ADDQ,
  ROW_SUBQ,
  ROW_ADDI_DN,
  ROW_COUNT,
};

static const ap_m68030_table_entry_t TABLE[ROW_COUNT] = {
    /* §11.6.8, Arithmetical/Logical Instructions. */
    [ROW_ADD_RN_DN] = {"ADD Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDA_W] = {"ADDA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDA_L] = {"ADDA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_AND_DN_DN] = {"AND Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_EOR_DN_DN] = {"EOR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_OR_DN_DN] = {"OR Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUB_RN_DN] = {"SUB Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBA_W] = {"SUBA.W Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBA_L] = {"SUBA.L Rn,An", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CMP_RN_DN] = {"CMP Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CMPA_RN_AN] = {"CMPA Rn,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* The divides, marked `+` in the table: "Indicates Maximum Time (Actual
     * time is data dependent)". PROVISIONAL. */
    [ROW_DIVS_W] = {"DIVS.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 56, .no_cache_case = 56, .prefetches = 1}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVS_L] = {"DIVS.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 90, .no_cache_case = 90, .prefetches = 1}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVU_W] = {"DIVU.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 44, .no_cache_case = 44, .prefetches = 1}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVU_L] = {"DIVU.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 78, .no_cache_case = 78, .prefetches = 1}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* The memory-destination forms. These are the first rows whose `NCC`
     * exceeds their `CC`: `3(0/0/1)` against `4(0/1/1)`, so the write hides
     * under three clocks of microcode but the write *plus* a prefetch does not.
     * Under `max(microcode, bus)` the microcode is `CC` here as elsewhere --
     * max(3,2) = 3 and max(3,4) = 4 -- and the core's own bus time supplies the
     * rest. They are what exercises the model where the register forms cannot. */
    [ROW_ADD_DN_EA] = {"ADD Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1},
     false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUB_DN_EA] = {"SUB Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1},
     false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_AND_DN_EA] = {"AND Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1},
     false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_OR_DN_EA] = {"OR Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1},
     false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_EOR_DN_EA] = {"EOR Dn,EA", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1},
     false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.6, the MOVE instruction, register-source forms.
     *
     * `MOVE Rn,-(An)` is the row worth noticing: `CC 4(0/0/1)` where the other
     * memory destinations are 3, and a **tail of 2** where they have 1. The
     * predecrement costs a clock the postincrement does not, which is the kind
     * of asymmetry a model built from a single "memory destination" cost would
     * flatten. */
    [ROW_MOVE_RN_DN] = {"MOVE Rn,Dn", {.head = 2, .tail = 0, .cache_case = 2,
                         .no_cache_case = 2, .prefetches = 1},
                        false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_RN_AN] = {"MOVE Rn,An", {.head = 2, .tail = 0, .cache_case = 2,
                         .no_cache_case = 2, .prefetches = 1},
                        false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_RN_IND] = {"MOVE Rn,(An)", {.head = 0, .tail = 1, .cache_case = 3,
                          .no_cache_case = 4, .writes = 1, .prefetches = 1},
                         false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_RN_POSTINC] = {"MOVE Rn,(An)+", {.head = 0, .tail = 1, .cache_case = 3,
                              .no_cache_case = 4, .writes = 1, .prefetches = 1},
                             false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_RN_PREDEC] = {"MOVE Rn,-(An)", {.head = 0, .tail = 2, .cache_case = 4,
                             .no_cache_case = 4, .writes = 1, .prefetches = 1},
                            false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.11, Single Operand Instructions, register forms. */
    [ROW_CLR_DN] = {"CLR Dn", {2, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NEG_DN] = {"NEG Dn", {2, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NEGX_DN] = {"NEGX Dn", {2, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NOT_DN] = {"NOT Dn", {2, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_EXT_DN] = {"EXT Dn", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    /* TST is the one with a head of zero: nothing of it can be absorbed by the
     * previous instruction's tail, unlike its neighbours. */
    [ROW_TST_DN] = {"TST Dn", {0, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SCC_DN] = {"Scc Dn", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_TAS_DN] = {"TAS Dn", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NBCD_DN] = {"NBCD Dn", {0, 0, 6, 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.12, Shift/Rotate Instructions, immediate-count register forms.
     *
     * "The number of bits shifted does not affect the execution time, unless
     * noted" -- and the noted rows are the register-count forms, marked `%`
     * for a count within the operand size and `+` for one beyond it. Those are
     * count-dependent and are not transcribed here; only the immediate-count
     * forms, whose cost is fixed.
     *
     * **ASL costs more than ASR**: 6 against 4 for the same immediate count.
     * That is not an oddity of the table -- ASL must watch the sign bit, since
     * "V is set if the most significant bit is changed at any time during the
     * shift operation", and ASR has no such rule. The extra clocks are the
     * extra work, and `ap_m68030_alu_shift` already does exactly that work. */
    [ROW_LS_IMM] = {"LSd #<data>,Dy", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASL_IMM] = {"ASL #<data>,Dy", {2, 0, 6, 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASR_IMM] = {"ASR #<data>,Dy", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_RO_IMM] = {"ROd #<data>,Dy", {4, 0, 6, 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ROX_DN] = {"ROXd Dn", {10, 0, 12, 12, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.16, Control Instructions -- the forms whose cost is fixed by the
     * instruction word alone.
     *
     * `NOP` has a **head of zero**, unlike most register operations, so nothing
     * of it can be absorbed by the previous instruction's tail. An instruction
     * that does nothing still cannot be overlapped away.
     *
     * The returns carry operand reads in their cache case -- `RTS` is
     * `9(1/0/0)`, one read for the return address -- so their `CC` already
     * includes two clocks of bus. Under `max(microcode, bus)` that is still the
     * microcode figure, since every one of these exceeds its own bus time. */
    [ROW_NOP] = {"NOP", {0, 0, 2, 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_RTS] = {"RTS", {1, 0, 9, 11, .reads = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTR] = {"RTR", {1, 0, 12, 14, .reads = 2, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTD] = {"RTD", {2, 0, 10, 12, .reads = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_UNLK] = {"UNLK", {0, 0, 5, 5, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_LINK_W] = {"LINK.W", {0, 0, 4, 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_LINK_L] = {"LINK.L", {2, 0, 6, 7, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ODD_WORDS},

    /* The six logical-immediate-to-status forms share one row at 12 clocks.
     * That is six times the cost of the same operation on a data register,
     * which is the price of a status register write forcing the pipe to
     * refill -- the same fact §8.1.7 gives as the reason those instructions
     * count as a change of flow for tracing. */
    [ROW_LOGICAL_TO_SR] = {"ANDI/EORI/ORI to SR or CCR", {4, 0, 12, 14, .prefetches = 2},
                           false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.15, Conditional Branch Instructions. "Complete execution times
     * given. No additional tables are needed" -- so unlike most rows these are
     * whole costs rather than a part needing an effective address time.
     *
     * A *taken* branch is one row whatever its displacement size; an untaken
     * one distinguishes byte, word and long. That asymmetry is the pipe: a
     * taken branch throws it away regardless of how far it jumped, while an
     * untaken one has merely read a displacement of some length. */
    [ROW_BCC_TAKEN] = {"Bcc (Taken)", {6, 0, 6, 8, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCC_B_NOT_TAKEN] = {"Bcc.B (Not Taken)", {4, 0, 4, 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BCC_W_NOT_TAKEN] = {"Bcc.W (Not Taken)", {6, 0, 6, 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCC_L_NOT_TAKEN] = {"Bcc.L (Not Taken)", {6, 0, 6, 8, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_BSR] = {"BSR", {2, 0, 6, 9, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* DBcc has three cases, and the expensive one is *leaving* the loop with
     * the counter expired: 10 clocks against 6 for going round again. */
    [ROW_DBCC_LOOPING] = {"DBcc (cc False, Count Not Expired)", {6, 0, 6, 8, .prefetches = 2},
                          false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_DBCC_EXPIRED] = {"DBcc (cc False, Count Expired)", {10, 0, 10, 13, .prefetches = 3},
                          false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_DBCC_TRUE] = {"DBcc (cc True)", {6, 0, 6, 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.9, Immediate Arithmetical/Logical Instructions. */
    [ROW_MOVEQ] = {"MOVEQ #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDQ] = {"ADDQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBQ] = {"SUBQ #<data>,Rn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    /* `**` in the table: the immediate is fetched through a separate effective
     * address time, so this figure is not the whole cost. */
    [ROW_ADDI_DN] = {"ADDI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
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

  /* §11.6.16's control instructions. The `$4E` group's fixed-cost members are
   * identified by their whole instruction word, since each is a single
   * encoding rather than a family. */
  switch (instruction) {
  case 0x4E71u:
    return &TABLE[ROW_NOP];
  case 0x4E75u:
    return &TABLE[ROW_RTS];
  case 0x4E77u:
    return &TABLE[ROW_RTR];
  case 0x4E74u:
    return &TABLE[ROW_RTD];
  default:
    break;
  }
  /* LINK and UNLK carry a register in their low three bits, so they are ranges
   * rather than single words. LINK.W is `$4E5x`, UNLK `$4E5x` above it, and
   * LINK.L a family 0100 form at `$480x`. */
  if ((instruction & 0xFFF8u) == 0x4E50u) {
    return &TABLE[ROW_LINK_W];
  }
  if ((instruction & 0xFFF8u) == 0x4E58u) {
    return &TABLE[ROW_UNLK];
  }
  if ((instruction & 0xFFF8u) == 0x4808u) {
    return &TABLE[ROW_LINK_L];
  }

  /* §11.6.11's single-operand forms, family 0100. Bits 11-9 choose the
   * operation and bits 7-6 the size, with `11` an escape to a different
   * instruction entirely -- so a size of `11` is not a wider operand here and
   * is refused rather than mapped. Only the data-register form is transcribed;
   * the memory forms carry a `*` or `**` effective address time. */
  if (family == 0x4u) {
    const unsigned row = (unsigned)((instruction >> 9) & 0x7u);
    const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
    if (size_field == 0x3u || mode != 0x0u) {
      return nullptr;
    }
    switch (row) {
    case 0x0u:
      return &TABLE[ROW_NEGX_DN];
    case 0x1u:
      return &TABLE[ROW_CLR_DN];
    case 0x2u:
      return &TABLE[ROW_NEG_DN];
    case 0x3u:
      return &TABLE[ROW_NOT_DN];
    case 0x5u:
      return &TABLE[ROW_TST_DN];
    default:
      return nullptr;
    }
  }

  /* §11.6.12's shifts, family 1110. Bits 4-3 name the type, bit 8 the
   * direction and bit 5 whether the count is immediate or in a register. Only
   * the immediate-count forms are transcribed: the register-count rows are
   * marked `%` and `+` for counts within and beyond the operand size, so their
   * cost depends on a value the table cannot publish. */
  if (family == 0xEu) {
    /* Bits 5-3 are **not** an addressing mode here: bit 5 says where the count
     * comes from and bits 4-3 name the shift type. The memory forms are the
     * ones whose size field reads `11`, which is the same escape-not-a-size
     * idiom family 0100 uses -- and they shift by one through an effective
     * address, so they are not transcribed.
     *
     * Reading bits 5-3 as a mode here is what an earlier version did, and it
     * rejected ROR while admitting LSR purely by where their type bits fell. */
    if (((instruction >> 6) & 0x3u) == 0x3u) {
      return nullptr;
    }
    const unsigned type = (unsigned)((instruction >> 3) & 0x3u);
    const bool count_in_register = ((instruction >> 5) & 1u) != 0u;
    const bool left = ((instruction >> 8) & 1u) != 0u;
    if (count_in_register) {
      return nullptr; /* count-dependent; see the table's %% and + markers */
    }
    switch (type) {
    case 0x0u: /* arithmetic: the one direction that costs more than the other */
      return left ? &TABLE[ROW_ASL_IMM] : &TABLE[ROW_ASR_IMM];
    case 0x1u:
      return &TABLE[ROW_LS_IMM];
    case 0x2u:
      return &TABLE[ROW_ROX_DN];
    case 0x3u:
      return &TABLE[ROW_RO_IMM];
    default:
      return nullptr;
    }
  }

  /* MOVE and MOVEA, families 0001, 0010 and 0011. The destination's mode sits
   * in bits 8-6 and its register in 11-9, reversed from the source -- which is
   * the field order that has caught this project out before. Only a register
   * source is transcribed; a memory source needs a fetch effective address time
   * this module does not carry. */
  if (family >= 0x1u && family <= 0x3u) {
    const unsigned destination_mode = (unsigned)((instruction >> 6) & 0x7u);
    if (!register_source) {
      return nullptr;
    }
    switch (destination_mode) {
    case 0x0u:
      return &TABLE[ROW_MOVE_RN_DN];
    case 0x1u:
      return &TABLE[ROW_MOVE_RN_AN];
    case 0x2u:
      return &TABLE[ROW_MOVE_RN_IND];
    case 0x3u:
      return &TABLE[ROW_MOVE_RN_POSTINC];
    case 0x4u:
      return &TABLE[ROW_MOVE_RN_PREDEC];
    default:
      return nullptr;
    }
  }

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

const ap_m68030_table_entry_t *ap_m68030_timing_for_branch(uint16_t instruction,
                                                           bool taken) {
  if (((instruction >> 12) & 0xFu) != 0x6u) {
    return nullptr;
  }

  /* BSR is condition `F` in the encoding -- the value that means "never" for a
   * Bcc -- and is unconditional, so `taken` does not apply to it. Reading the
   * condition field without excluding BSR is the same trap the step itself
   * fell into once. */
  const unsigned condition = (unsigned)((instruction >> 8) & 0xFu);
  if (condition == 0x1u) {
    return &TABLE[ROW_BSR];
  }

  if (taken) {
    /* One row whatever the displacement size. */
    return &TABLE[ROW_BCC_TAKEN];
  }

  /* Not taken, and now the size matters: the displacement byte is zero for the
   * word form and $FF for the long one, which is how the encoding names a size
   * it has no field for. */
  const unsigned displacement = (unsigned)(instruction & 0xFFu);
  if (displacement == 0x00u) {
    return &TABLE[ROW_BCC_W_NOT_TAKEN];
  }
  if (displacement == 0xFFu) {
    return &TABLE[ROW_BCC_L_NOT_TAKEN];
  }
  return &TABLE[ROW_BCC_B_NOT_TAKEN];
}

const ap_m68030_table_entry_t *ap_m68030_timing_for_dbcc(bool condition_true,
                                                         bool count_expired) {
  if (condition_true) {
    return &TABLE[ROW_DBCC_TRUE];
  }
  return count_expired ? &TABLE[ROW_DBCC_EXPIRED] : &TABLE[ROW_DBCC_LOOPING];
}
