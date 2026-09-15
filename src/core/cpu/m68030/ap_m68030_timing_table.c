/* MC68030 published instruction timings, `[030]` §11.6. See the header for
 * which rows are transcribed and why the rest are not. */

#include "cpu/m68030/ap_m68030_timing_table.h"

#include "cpu/m68030/ap_m68030_ea.h"
#include "cpu/m68030/ap_m68030_exception.h"

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
  ROW_RESET,
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
  /* §11.6.9's remaining rows, 2026-09-14. */
  ROW_ADDQ_MEM,
  ROW_SUBQ_MEM,
  ROW_ADDI_MEM,
  ROW_ANDI_DN,
  ROW_ANDI_MEM,
  ROW_EORI_DN,
  ROW_EORI_MEM,
  ROW_ORI_DN,
  ROW_ORI_MEM,
  ROW_SUBI_DN,
  ROW_SUBI_MEM,
  ROW_CMPI_DN,
  ROW_CMPI_MEM,
  /* §11.6.13, whole. */
  ROW_BTST_IMM_DN,
  ROW_BTST_DN_DN,
  ROW_BTST_IMM_MEM,
  ROW_BTST_DN_MEM,
  ROW_BCHG_IMM_DN,
  ROW_BCHG_DN_DN,
  ROW_BCHG_IMM_MEM,
  ROW_BCHG_DN_MEM,
  ROW_BCLR_IMM_DN,
  ROW_BCLR_DN_DN,
  ROW_BCLR_IMM_MEM,
  ROW_BCLR_DN_MEM,
  ROW_BSET_IMM_DN,
  ROW_BSET_DN_DN,
  ROW_BSET_IMM_MEM,
  ROW_BSET_DN_MEM,
  /* §11.6.6's single effective address format, the memory and immediate
   * sources. */
  ROW_MOVE_EA_DN,
  ROW_MOVE_EA_AN,
  ROW_MOVE_SOURCE_IND,
  ROW_MOVE_SOURCE_POSTINC,
  ROW_MOVE_SOURCE_PREDEC,
  ROW_MOVE_EA_D16_AN,
  ROW_MOVE_EA_ABS_W,
  ROW_MOVE_EA_ABS_L,
  /* §11.6.8's memory-source `*` rows. */
  ROW_ADD_EA_DN,
  ROW_ADDA_W_EA,
  ROW_ADDA_L_EA,
  ROW_AND_EA_DN,
  ROW_OR_EA_DN,
  ROW_SUB_EA_DN,
  ROW_SUBA_W_EA,
  ROW_SUBA_L_EA,
  ROW_CMP_EA_DN,
  ROW_CMPA_EA_AN,
  ROW_MULS_W_EA,
  ROW_MULU_W_EA,
  ROW_DIVS_W_EA,
  ROW_DIVU_W_EA,
  /* §11.6.10, whole. */
  ROW_ABCD_DN,
  ROW_ABCD_PREDEC,
  ROW_SBCD_DN,
  ROW_SBCD_PREDEC,
  ROW_ADDX_DN,
  ROW_ADDX_PREDEC,
  ROW_SUBX_DN,
  ROW_SUBX_PREDEC,
  ROW_CMPM,
  ROW_PACK_DN,
  ROW_PACK_PREDEC,
  ROW_UNPK_DN,
  ROW_UNPK_PREDEC,
  /* §11.6.11's memory forms. */
  ROW_CLR_MEM,
  ROW_NEG_MEM,
  ROW_NEGX_MEM,
  ROW_NOT_MEM,
  ROW_SCC_MEM,
  ROW_TAS_MEM,
  ROW_TST_MEM,
  /* §11.6.12's memory shifts and the two fixed register-count rows. */
  ROW_LS_MEM,
  ROW_ASL_DX,
  ROW_ASL_MEM,
  ROW_ASR_MEM,
  ROW_RO_DX,
  ROW_RO_MEM,
  ROW_ROX_MEM,
  /* §11.6.16's address forms, and §11.6.7's rows the instruction word
   * selects. */
  ROW_JMP,
  ROW_JSR,
  ROW_LEA,
  ROW_PEA,
  ROW_EXG,
  ROW_MOVEC_CR_RN,
  ROW_MOVE_CCR_DN,
  ROW_MOVE_CCR_MEM,
  ROW_MOVE_DN_CCR,
  ROW_MOVE_EA_CCR,
  ROW_MOVE_SR_DN,
  ROW_MOVE_SR_MEM,
  ROW_MOVE_EA_SR,
  ROW_MOVEP_W_TO_MEM,
  ROW_MOVEP_W_FROM_MEM,
  ROW_MOVEP_L_TO_MEM,
  ROW_MOVEP_L_FROM_MEM,
  ROW_MOVE_USP_AN,
  ROW_MOVE_AN_USP,
  ROW_SWAP,
  /* The rows an extension word or an outcome selects: §11.6.7, §11.6.14 and
   * §11.6.16. */
  ROW_MOVEC_RN_CR_A,
  ROW_MOVEC_RN_CR_B,
  ROW_MOVES_EA_RN,
  ROW_MOVES_RN_EA,
  ROW_BFTST_DN,
  ROW_BFTST_MEM,
  ROW_BFTST_MEM5,
  ROW_BFCHG_DN,
  ROW_BFCHG_MEM,
  ROW_BFCHG_MEM5,
  ROW_BFCLR_DN,
  ROW_BFCLR_MEM,
  ROW_BFCLR_MEM5,
  ROW_BFSET_DN,
  ROW_BFSET_MEM,
  ROW_BFSET_MEM5,
  ROW_BFEXTS_DN,
  ROW_BFEXTS_MEM,
  ROW_BFEXTS_MEM5,
  ROW_BFEXTU_DN,
  ROW_BFEXTU_MEM,
  ROW_BFEXTU_MEM5,
  ROW_BFINS_DN,
  ROW_BFINS_MEM,
  ROW_BFINS_MEM5,
  ROW_BFFFO_DN,
  ROW_BFFFO_MEM,
  ROW_BFFFO_MEM5,
  ROW_CAS_MATCH,
  ROW_CAS_MISMATCH,
  ROW_CAS2_MATCH,
  ROW_CAS2_MISMATCH,
  /* §11.6.17's remaining rows, beside `RESET`. */
  ROW_BKPT,
  ROW_INTERRUPT_I,
  ROW_INTERRUPT_M,
  ROW_STOP,
  ROW_TRACE,
  ROW_TRAP_N,
  ROW_ILLEGAL,
  ROW_LINE_A,
  ROW_LINE_F,
  ROW_PRIVILEGE,
  ROW_TRAPCC_TRAP,
  ROW_TRAPCC_NO_TRAP,
  ROW_TRAPCC_W_TRAP,
  ROW_TRAPCC_W_NO_TRAP,
  ROW_TRAPCC_L_TRAP,
  ROW_TRAPCC_L_NO_TRAP,
  ROW_TRAPV_TRAP,
  ROW_TRAPV_NO_TRAP,
  /* §11.6.18, whole. */
  ROW_BUS_FAULT_SHORT,
  ROW_BUS_FAULT_LONG,
  ROW_RTE_NORMAL,
  ROW_RTE_SIX_WORD,
  ROW_RTE_THROWAWAY,
  ROW_RTE_COPROCESSOR,
  ROW_RTE_SHORT_FAULT,
  ROW_RTE_LONG_FAULT,
  /* §11.6.16's bounds checks. */
  ROW_CHK_DN_DN,
  ROW_CHK_DN_DN_TAKEN,
  ROW_CHK_EA_DN,
  ROW_CHK_EA_DN_TAKEN,
  ROW_CHK2,
  ROW_CHK2_TAKEN,
  /* §11.6.8's rows the extension word selects. */
  ROW_CMP2,
  ROW_MULS_L_EA,
  ROW_MULU_L_EA,
  ROW_DIVS_L_EA,
  ROW_DIVU_L_EA,
  /* §11.6.6's mode-6 destinations. */
  ROW_MOVE_BRIEF,
  ROW_MOVE_FULL_A_NONE,
  ROW_MOVE_FULL_A_OD_NULL,
  ROW_MOVE_FULL_A_OD_WORD,
  ROW_MOVE_FULL_A_OD_LONG,
  ROW_MOVE_FULL_NONE_BD_NULL,
  ROW_MOVE_FULL_NONE_BD_WORD,
  ROW_MOVE_FULL_NONE_BD_LONG,
  ROW_MOVE_FULL_OD_NULL_BD_NULL,
  ROW_MOVE_FULL_OD_NULL_BD_WORD,
  ROW_MOVE_FULL_OD_NULL_BD_LONG,
  ROW_MOVE_FULL_OD_WORD_BD_NULL,
  ROW_MOVE_FULL_OD_WORD_BD_WORD,
  ROW_MOVE_FULL_OD_WORD_BD_LONG,
  ROW_MOVE_FULL_OD_LONG_BD_NULL,
  ROW_MOVE_FULL_OD_LONG_BD_WORD,
  ROW_MOVE_FULL_OD_LONG_BD_LONG,
  /* §11.6.12's register-count `LSd` and `ASR`, which the run's count selects. */
  ROW_LS_DX_WITHIN,
  ROW_LS_DX_BEYOND,
  ROW_ASR_DX_WITHIN,
  ROW_ASR_DX_BEYOND,
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
    /* The long divides are `**` on the page, not unmarked as they were first
     * written here: the extension word is fetched through §11.6.2. Unreachable
     * by a one-word lookup either way, so this is fidelity, not a price. */
    [ROW_DIVS_L] = {"DIVS.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 90, .no_cache_case = 90, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_DIVU_W] = {"DIVU.W Dn,Dn", {.head = 2, .tail = 0, .cache_case = 44, .no_cache_case = 44, .prefetches = 1}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVU_L] = {"DIVU.L Dn,Dn", {.head = 6, .tail = 0, .cache_case = 78, .no_cache_case = 78, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

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
    /* §11.6.17, and the one row here from outside §11.6.8/§11.6.9. It is almost
     * all `RSTO` assertion rather than microcode -- `[PRM]`'s `RESET` page gives
     * "512 ... clock periods" against this 518 -- which is why it dwarfs every
     * other entry and why it is not data-dependent despite doing so. */
    [ROW_RESET] = {"RESET", {0, 0, 518, 518, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
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

    /* The rest of §11.6.9, read as the page image (p. 11-42), 2026-09-14.
     *
     * The five `#<data>,Dn` forms are `ADDI`'s row five more times, and the
     * memory forms are `ADD Dn,EA`'s shape -- `3(0/0/1)` against `4(0/1/1)`,
     * one write -- except **`CMPI #<data>,Mem`**, which is `2(0/0/0)` with a
     * tail of 0: a compare reads its destination and never writes it back, so
     * the write and the tail it would have left both go. `ADDQ`/`SUBQ` to
     * memory are `*`, fetch effective address; every `I` form is `**`, the
     * immediate-and-destination table. */
    [ROW_ADDQ_MEM] = {"ADDQ #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBQ_MEM] = {"SUBQ #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDI_MEM] = {"ADDI #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_ANDI_DN] = {"ANDI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_ANDI_MEM] = {"ANDI #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_EORI_DN] = {"EORI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_EORI_MEM] = {"EORI #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_ORI_DN] = {"ORI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_ORI_MEM] = {"ORI #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_SUBI_DN] = {"SUBI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_SUBI_MEM] = {"SUBI #<data>,Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CMPI_DN] = {"CMPI #<data>,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CMPI_MEM] = {"CMPI #<data>,Mem", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.13, Bit Manipulation Instructions, whole (p. 11-46, page image).
     *
     * **`BTST` is two clocks cheaper than the other three in every form**, 4
     * against 6 -- and it is the only one of the four with no write in its
     * memory forms, so that is not the whole difference: the register forms,
     * which write nothing either, still differ by two. The `#<data>` register
     * forms are two words, the bit number riding in an extension, and carry no
     * footnote because the table prices that word itself; the memory forms are
     * `#`, "Add Fetch Immediate Effective Address Time", the same §11.6.2 table
     * `**` names elsewhere, and `*` for the dynamic `Dn,Mem` forms. */
    [ROW_BTST_IMM_DN] = {"BTST #<data>,Dn", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BTST_DN_DN] = {"BTST Dn,Dn", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BTST_IMM_MEM] = {"BTST #<data>,Mem", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BTST_DN_MEM] = {"BTST Dn,Mem", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BCHG_IMM_DN] = {"BCHG #<data>,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCHG_DN_DN] = {"BCHG Dn,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BCHG_IMM_MEM] = {"BCHG #<data>,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCHG_DN_MEM] = {"BCHG Dn,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BCLR_IMM_DN] = {"BCLR #<data>,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCLR_DN_DN] = {"BCLR Dn,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BCLR_IMM_MEM] = {"BCLR #<data>,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BCLR_DN_MEM] = {"BCLR Dn,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BSET_IMM_DN] = {"BSET #<data>,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BSET_DN_DN] = {"BSET Dn,Dn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_BSET_IMM_MEM] = {"BSET #<data>,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BSET_DN_MEM] = {"BSET Dn,Mem", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.6, the single effective address format's `*` rows (p. 11-37,
     * page image): every source that is not a register, composed with §11.6.1
     * for the source, since "the fetch effective address table is needed on
     * most MOVE operations (source, destination dependent)" and the MOVE
     * table's own figure already includes the destination address.
     *
     * **A register source moving into `(d16,An)` or an absolute address is one
     * of these rows too**: the table has no `MOVE Rn,xxx.L`, only `MOVE
     * EA,xxx.L`, and `EA` "is any Effective Address". §11.6.1's register rows
     * are what make that composition cost nothing extra.
     *
     * The brief-format row `MOVE EA,(d8,An,Xn)` and every full-format row are
     * **not** here: mode 6 is one field for both formats, and which one it is
     * lives in an extension word this lookup is not given. Named rather than
     * guessed. */
    [ROW_MOVE_EA_DN] = {"MOVE EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_EA_AN] = {"MOVE EA,An", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_SOURCE_IND] = {"MOVE SOURCE,(An)", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_SOURCE_POSTINC] = {"MOVE SOURCE,(An)+", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_SOURCE_PREDEC] = {"MOVE SOURCE,-(An)", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_EA_D16_AN] = {"MOVE EA,(d16,An)", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_EA_ABS_W] = {"MOVE EA,xxx.W", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_EA_ABS_L] = {"MOVE EA,xxx.L", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 7, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},

    /* §11.6.8's `*` rows for a memory source, pp. 11-40 and 11-41 as page
     * images. Each is its register row with the head taken away -- `ADD Rn,Dn`
     * is head 2 and `ADD EA,Dn` head 0 -- because the effective address
     * calculation now sits in front of the operation and is what overlaps the
     * previous instruction's tail. The totals do not change: the word-size
     * address forms are still 4 and the long ones 2.
     *
     * The multiplies appear **only** as `EA,Dn`, never as `Dn,Dn`, so a
     * register multiply is this row composed with §11.6.1's register row. The
     * word multiplies and divides carry `+`, so they are `PROVISIONAL` with the
     * divides already here. The long forms are `**` and selected by their
     * extension word, which a one-word lookup cannot see. */
    [ROW_ADD_EA_DN] = {"ADD EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDA_W_EA] = {"ADD.W EA,An", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDA_L_EA] = {"ADDA.L EA,An", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_AND_EA_DN] = {"AND EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_OR_EA_DN] = {"OR EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUB_EA_DN] = {"SUB EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBA_W_EA] = {"SUBA.W EA,An", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBA_L_EA] = {"SUBA.L EA,An", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CMP_EA_DN] = {"CMP EA,Dn", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CMPA_EA_AN] = {"CMPA EA,An", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MULS_W_EA] = {"MULS.W EA,Dn", {.head = 2, .tail = 0, .cache_case = 28, .no_cache_case = 28, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MULU_W_EA] = {"MULU.W EA,Dn", {.head = 2, .tail = 0, .cache_case = 28, .no_cache_case = 28, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVS_W_EA] = {"DIVS.W EA,Dn", {.head = 0, .tail = 0, .cache_case = 56, .no_cache_case = 56, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_DIVU_W_EA] = {"DIVU.W EA,Dn", {.head = 0, .tail = 0, .cache_case = 44, .no_cache_case = 44, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.10, Binary-Coded Decimal and Extended Instructions, whole (p. 11-43
     * as page image). "No additional tables are needed", so every row is a
     * whole cost.
     *
     * These seven share their families' bits with the register arithmetic --
     * `ABCD` is `AND`'s family with the direction bit set and a register
     * operand -- and until they had rows **the lookup priced them as that
     * arithmetic**: `ABCD -(A0),-(A1)` came back as `AND Dn,Dn`, 2 clocks for
     * an instruction the table gives 13, with two reads and a write. The
     * predecrement forms are the expensive ones, and the BCD pair costs four
     * clocks more than `ADDX`/`SUBX` for the decimal adjust. `PACK` and `UNPK`
     * carry an adjustment word, so they are two words. */
    [ROW_ABCD_DN] = {"ABCD Dn,Dn", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ABCD_PREDEC] = {"ABCD -(An),-(An)", {.head = 2, .tail = 1, .cache_case = 13, .no_cache_case = 14, .reads = 2, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SBCD_DN] = {"SBCD Dn,Dn", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SBCD_PREDEC] = {"SBCD -(An),-(An)", {.head = 2, .tail = 1, .cache_case = 13, .no_cache_case = 14, .reads = 2, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDX_DN] = {"ADDX Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ADDX_PREDEC] = {"ADDX -(An),-(An)", {.head = 2, .tail = 1, .cache_case = 9, .no_cache_case = 10, .reads = 2, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBX_DN] = {"SUBX Dn,Dn", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SUBX_PREDEC] = {"SUBX -(An),-(An)", {.head = 2, .tail = 1, .cache_case = 9, .no_cache_case = 10, .reads = 2, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CMPM] = {"CMPM (An)+,(An)+", {.head = 0, .tail = 0, .cache_case = 8, .no_cache_case = 8, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_PACK_DN] = {"PACK Dn,Dn,#<data>", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_PACK_PREDEC] = {"PACK -(An),-(An),#<data>", {.head = 2, .tail = 1, .cache_case = 11, .no_cache_case = 11, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_UNPK_DN] = {"UNPK Dn,Dn,#<data>", {.head = 8, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_UNPK_PREDEC] = {"UNPK -(An),-(An),#<data>", {.head = 2, .tail = 1, .cache_case = 11, .no_cache_case = 11, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.11's memory forms, p. 11-44 as page image. The footnotes split the
     * table in two and mean what they say: `NEG`, `NEGX`, `NOT` and `TST` are
     * `*`, fetch -- they read their operand -- while `CLR`, `Scc` and `TAS` are
     * `**`, calculate. `CLR` and `Scc` write a value that does not depend on
     * the old one, so there is no read to fetch. **`TAS` is `**` and still
     * carries a read**, `12(1/0/1)`: its read is the indivisible half of the
     * read-modify-write, and it is priced in the row rather than in the
     * address. That is also why its head is 3 where the others are 0. */
    [ROW_CLR_MEM] = {"CLR Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NEG_MEM] = {"NEG Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NEGX_MEM] = {"NEGX Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_NOT_MEM] = {"NOT Mem", {.head = 0, .tail = 1, .cache_case = 3, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SCC_MEM] = {"Scc Mem", {.head = 0, .tail = 1, .cache_case = 5, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_TAS_MEM] = {"TAS Mem", {.head = 3, .tail = 0, .cache_case = 12, .no_cache_case = 12, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_TST_MEM] = {"TST Mem", {.head = 0, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.12's memory shifts, all `*` and all by one bit, and the two
     * register-count rows the page prints **without** `%` or `+`: `ASL Dx,Dy`
     * and `ROd Dx,Dy` cost 8 whatever the count, where `LSd` and `ASR` split
     * by count -- the four rows after `ROd Dx,Dy`. `ASL` is again dearer than `ASR` in memory, 6
     * against 4, for the same reason as the register form: it watches the sign.
     *
     * **`ROXd Mem by 1` disagrees with itself on the page**: `4(0/0/1)` in the
     * cache case and `4(0/1/0)` in the no-cache case, a write in one and not
     * the other. The instruction writes its operand back, so the cache case's
     * count is taken, and the other is recorded here rather than silently
     * dropped. */
    [ROW_LS_MEM] = {"LSd Mem by 1", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASL_DX] = {"ASL Dx,Dy", {.head = 4, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASL_MEM] = {"ASL Mem by 1", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASR_MEM] = {"ASR Mem by 1", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_RO_DX] = {"ROd Dx,Dy", {.head = 6, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    /* The register-count rows the page prints twice (p. 11-45): `%`, "shift
     * count is less than or equal to the size of data", and `+`, "shift count
     * is greater than size of data". The count is the register's modulo 64,
     * as the shift itself takes it. */
    [ROW_LS_DX_WITHIN] = {"LSd Dx,Dy (Count <= Size)", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_LS_DX_BEYOND] = {"LSd Dx,Dy (Count > Size)", {.head = 8, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASR_DX_WITHIN] = {"ASR Dx,Dy (Count <= Size)", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ASR_DX_BEYOND] = {"ASR Dx,Dy (Count > Size)", {.head = 10, .tail = 0, .cache_case = 10, .no_cache_case = 10, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_RO_MEM] = {"ROd Mem by 1", {.head = 0, .tail = 0, .cache_case = 6, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_ROX_MEM] = {"ROXd Mem by 1", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.16's address forms, from the page image (p. 11-49). `JMP` and `JSR`
     * are `%`, "Add Jump Effective Address Time", §11.6.5; `LEA` and `PEA` are
     * `**`, §11.6.3. The two jumps refill the pipe at their target, so both
     * publish two prefetches and neither depends on alignment -- `BSR`'s shape,
     * and `JSR`'s figures are `BSR`'s less the displacement word. */
    [ROW_JMP] = {"JMP", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 6, .prefetches = 2}, false, AP_M68030_EA_TIME_JUMP, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_JSR] = {"JSR", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 7, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_JUMP, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_LEA] = {"LEA", {.head = 2, .tail = 0, .cache_case = 2, .no_cache_case = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_PEA] = {"PEA", {.head = 0, .tail = 2, .cache_case = 4, .no_cache_case = 4, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.7, Special-Purpose MOVE, from the page image (p. 11-39): the rows
     * the instruction word selects. **Its footnote symbols are its own**: `*`
     * is Calculate Effective Address, `#` Fetch, and `%` Calculate Immediate --
     * the reverse of §11.6.8's `*` and a third meaning for §11.6.16's `%`.
     *
     * **`MOVE EA,CCR` is priced by the fetch table, against its footnote.** The
     * page marks it `*`, calculate, and prints no read in its row, so taken
     * literally a move into CCR from memory reads nothing anywhere. The
     * `MC68020 User's Manual` §9.2.7 marks the same instruction `*` as well --
     * but there `*` is "Add Fetch Effective Address time" and `#` is the
     * calculate table. The 68030 page swapped what the two symbols mean, moved
     * `MOVE EA,SR` and the two moves to memory with them, and left `EA,CCR`
     * under the old symbol. `MOVE EA,SR` is fetch on both pages. A symbol
     * carried over, then, and the instruction's operand read is what decides
     * it.
     *
     * Owed from this table: `MOVEC Rn,Cr` (the extension word picks group A or
     * B), both `MOVES` rows (the extension word picks the direction, priced
     * through §11.6.4) and both `MOVEM` rows (a formula in the register count
     * and wait states). */
    [ROW_EXG] = {"EXG Ry,Rx", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVEC_CR_RN] = {"MOVEC Cr,Rn", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_CCR_DN] = {"MOVE CCR,Dn", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_CCR_MEM] = {"MOVE CCR,Mem", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_DN_CCR] = {"MOVE Dn,CCR", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_EA_CCR] = {"MOVE EA,CCR", {.head = 0, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_SR_DN] = {"MOVE SR,Dn", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_SR_MEM] = {"MOVE SR,Mem", {.head = 2, .tail = 0, .cache_case = 4, .no_cache_case = 5, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE, AP_M68030_PREFETCH_SINGLE_WORD},
    /* A status register write refills the pipe -- two prefetches, as
     * `ANDI #<data>,SR` has -- and a refill costs the same at either alignment. */
    [ROW_MOVE_EA_SR] = {"MOVE EA,SR", {.head = 0, .tail = 0, .cache_case = 8, .no_cache_case = 10, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    /* Two words each: the displacement is the instruction's own, not an
     * effective address's, so it is in the row. */
    [ROW_MOVEP_W_TO_MEM] = {"MOVEP.W Dn,(d16,An)", {.head = 4, .tail = 0, .cache_case = 10, .no_cache_case = 10, .writes = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVEP_W_FROM_MEM] = {"MOVEP.W (d16,An),Dn", {.head = 2, .tail = 0, .cache_case = 10, .no_cache_case = 10, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVEP_L_TO_MEM] = {"MOVEP.L Dn,(d16,An)", {.head = 4, .tail = 0, .cache_case = 14, .no_cache_case = 14, .writes = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVEP_L_FROM_MEM] = {"MOVEP.L (d16,An),Dn", {.head = 2, .tail = 0, .cache_case = 14, .no_cache_case = 14, .reads = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_USP_AN] = {"MOVE USP,An", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_MOVE_AN_USP] = {"MOVE An,USP", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_SWAP] = {"SWAP Dn", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.7's rows the extension word selects. `MOVEC Rn,Cr` splits by the
     * page's register groups: A is USP, VBR, CAAR, MSP and ISP; B is SFC, DFC
     * and CACR, and costs twice as much. **The page prints group B's cache case
     * as `12(0/1/0)`**, a prefetch with the cache on -- writing CACR can flush
     * the cache under the instruction. The no-cache count is the one carried, as
     * for every row.
     *
     * `MOVES` is `%`, §11.6.4 through its extension word. **The page disagrees
     * with itself on both rows**: `MOVES EA,Rn` prints a prefetch in its cache
     * case, `7(1/1/0)`, and `MOVES Rn,EA` a read in its no-cache case only,
     * `5(0/0/1)` against `6(1/1/1)`. A move into memory has nothing to read, so
     * the cache case's read and write counts are taken, `ROXd Mem by 1`'s
     * ruling. */
    [ROW_MOVEC_RN_CR_A] = {"MOVEC Rn,Cr-A", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVEC_RN_CR_B] = {"MOVEC Rn,Cr-B", {.head = 4, .tail = 0, .cache_case = 12, .no_cache_case = 12, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVES_EA_RN] = {"MOVES EA,Rn", {.head = 3, .tail = 0, .cache_case = 7, .no_cache_case = 7, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVES_RN_EA] = {"MOVES Rn,EA", {.head = 2, .tail = 1, .cache_case = 5, .no_cache_case = 6, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.14, Bit Field Manipulation, from the page image (p. 11-47). The data
     * register forms are whole. The memory forms are `*`, which on this page is
     * "Add Calculate Immediate Effective Address Time" -- §11.6.4, a third
     * meaning for the symbol -- and each has two rows by the page's own note: "A
     * bit field of 32 bits may span 5 bytes that require two operand cycles to
     * access or may span 4 bytes that require only one operand cycle". Which ran
     * is the executor's `bitfield_span_t`, left as the step's outcome. Every
     * form is two words. */
    [ROW_BFTST_DN] = {"BFTST Dn", {.head = 8, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFTST_MEM] = {"BFTST Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 10, .no_cache_case = 10, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFTST_MEM5] = {"BFTST Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 14, .no_cache_case = 14, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCHG_DN] = {"BFCHG Dn", {.head = 14, .tail = 0, .cache_case = 14, .no_cache_case = 14, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCHG_MEM] = {"BFCHG Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 14, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCHG_MEM5] = {"BFCHG Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 22, .no_cache_case = 22, .reads = 2, .writes = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCLR_DN] = {"BFCLR Dn", {.head = 14, .tail = 0, .cache_case = 14, .no_cache_case = 14, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCLR_MEM] = {"BFCLR Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 14, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFCLR_MEM5] = {"BFCLR Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 22, .no_cache_case = 22, .reads = 2, .writes = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFSET_DN] = {"BFSET Dn", {.head = 14, .tail = 0, .cache_case = 14, .no_cache_case = 14, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFSET_MEM] = {"BFSET Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 14, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFSET_MEM5] = {"BFSET Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 22, .no_cache_case = 22, .reads = 2, .writes = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTS_DN] = {"BFEXTS Dn", {.head = 10, .tail = 0, .cache_case = 10, .no_cache_case = 10, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTS_MEM] = {"BFEXTS Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 12, .no_cache_case = 12, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTS_MEM5] = {"BFEXTS Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 18, .no_cache_case = 18, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTU_DN] = {"BFEXTU Dn", {.head = 10, .tail = 0, .cache_case = 10, .no_cache_case = 10, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTU_MEM] = {"BFEXTU Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 12, .no_cache_case = 12, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFEXTU_MEM5] = {"BFEXTU Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 18, .no_cache_case = 18, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFINS_DN] = {"BFINS Dn", {.head = 12, .tail = 0, .cache_case = 12, .no_cache_case = 12, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFINS_MEM] = {"BFINS Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 12, .no_cache_case = 12, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFINS_MEM5] = {"BFINS Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 18, .no_cache_case = 18, .reads = 2, .writes = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFFFO_DN] = {"BFFFO Dn", {.head = 20, .tail = 0, .cache_case = 20, .no_cache_case = 20, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFFFO_MEM] = {"BFFFO Mem (<5 Bytes)", {.head = 6, .tail = 0, .cache_case = 22, .no_cache_case = 22, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BFFFO_MEM5] = {"BFFFO Mem (5 Bytes)", {.head = 6, .tail = 0, .cache_case = 28, .no_cache_case = 28, .reads = 2, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.16's compare-and-swap rows, by outcome. `CAS` is `##`, §11.6.4
     * through its extension word; a mismatch writes the operand into `Dc`, a
     * register, which is why that row prints no write. `CAS2` is `+`,
     * "Indicates Maximum Time", so both its rows are `PROVISIONAL` as the divides
     * are, and it names no effective address table: both addresses are register
     * indirect through its two extension words, three words in all. */
    [ROW_CAS_MATCH] = {"CAS (Successful Compare)", {.head = 1, .tail = 0, .cache_case = 13, .no_cache_case = 13, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CAS_MISMATCH] = {"CAS (Unsuccessful Compare)", {.head = 1, .tail = 0, .cache_case = 11, .no_cache_case = 11, .reads = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CAS2_MATCH] = {"CAS2 (Successful Compare)", {.head = 2, .tail = 0, .cache_case = 24, .no_cache_case = 26, .reads = 2, .writes = 2, .prefetches = 2}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_CAS2_MISMATCH] = {"CAS2 (Unsuccessful Compare)", {.head = 2, .tail = 0, .cache_case = 24, .no_cache_case = 24, .reads = 2, .prefetches = 2}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ODD_WORDS},

    /* §11.6.17, Exception-Related Instructions and Operations, from the page
     * image (p. 11-50), beside `RESET` above. "No additional tables are needed
     * to calculate total effective execution time for these operations."
     *
     * The rows that take an exception publish the instruction and the
     * exception together: `TRAP #n`'s `18(1/0/4)` is the trap, the four-word
     * frame's four writes and the vector's one read. The step runs the frame
     * and measures that bus, so what it takes from these rows is the rest --
     * `ap_m68030_microcode_clocks` -- and the handler's refill, the no-cache
     * column's two prefetches, is measured in the next step. Their class is the
     * refill's: two fetches at either alignment.
     *
     * The `(No Trap)` rows are ordinary instructions and the word lookup's.
     * `TRAPcc` comes in three sizes of operand, the size deciding both rows;
     * `.W` and `.L` publish three prefetches when they trap, the operand's and
     * the refill's. */
    [ROW_BKPT] = {"BKPT", {.head = 1, .tail = 0, .cache_case = 9, .no_cache_case = 9, .reads = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_INTERRUPT_I] = {"Interrupt (I-Stack)", {.head = 0, .tail = 0, .cache_case = 23, .no_cache_case = 24, .reads = 2, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_INTERRUPT_M] = {"Interrupt (M-Stack)", {.head = 0, .tail = 0, .cache_case = 33, .no_cache_case = 34, .reads = 2, .writes = 8, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_STOP] = {"STOP", {.head = 0, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRACE] = {"TRACE", {.head = 0, .tail = 0, .cache_case = 22, .no_cache_case = 24, .reads = 1, .writes = 5, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAP_N] = {"TRAP #n", {.head = 0, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 1, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_ILLEGAL] = {"Illegal Instruction", {.head = 0, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 1, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_LINE_A] = {"A-Line Trap", {.head = 0, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 1, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_LINE_F] = {"F-Line Trap", {.head = 0, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 1, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_PRIVILEGE] = {"Privilege Violation", {.head = 0, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 1, .writes = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPCC_TRAP] = {"TRAPcc (Trap)", {.head = 2, .tail = 0, .cache_case = 22, .no_cache_case = 24, .reads = 1, .writes = 5, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPCC_NO_TRAP] = {"TRAPcc (No Trap)", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_TRAPCC_W_TRAP] = {"TRAPcc.W (Trap)", {.head = 5, .tail = 0, .cache_case = 24, .no_cache_case = 26, .reads = 1, .writes = 5, .prefetches = 3}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPCC_W_NO_TRAP] = {"TRAPcc.W (No Trap)", {.head = 6, .tail = 0, .cache_case = 6, .no_cache_case = 6, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPCC_L_TRAP] = {"TRAPcc.L (Trap)", {.head = 6, .tail = 0, .cache_case = 26, .no_cache_case = 28, .reads = 1, .writes = 5, .prefetches = 3}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPCC_L_NO_TRAP] = {"TRAPcc.L (No Trap)", {.head = 8, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_TRAPV_TRAP] = {"TRAPV (Trap)", {.head = 2, .tail = 0, .cache_case = 22, .no_cache_case = 24, .reads = 1, .writes = 5, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_TRAPV_NO_TRAP] = {"TRAPV (No Trap)", {.head = 4, .tail = 0, .cache_case = 4, .no_cache_case = 4, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},

    /* §11.6.18, Save and Restore Operations, from the page image (p. 11-51),
     * "with complete execution times and stack length given".
     *
     * The bus fault rows are exceptions like §11.6.17's and are charged the same
     * way, microcode on the bus the frame ran. **Their frames' cycle counts are
     * `[030]` §8.4's long-word rule**, which `take_bus_fault_with` follows: 10
     * writes short and **25** long, where the page prints **24** -- the 68020's
     * figure for its 44-word frame, which `M68030_WALK.md` records. The row is
     * transcribed as printed, so its microcode is 62 less 50.
     *
     * The `RTE` rows are instructions, chosen by the frame `RTE` unstacked. A
     * throwaway frame is priced on top of the frame behind it. **`RTE (Short
     * Fault)` prints its no-cache case as `26(10/2/0)` under a cache case of
     * `36(10/0/0)`**, the only row on either page whose no-cache case is less
     * than its cache case, and `[020]`'s own row runs 43 cached and 45 worst. A
     * digit slip for 36, taken as 36 -- the same no-cache-equals-cache shape the
     * long fault and throwaway rows print. */
    [ROW_BUS_FAULT_SHORT] = {"Bus Cycle Fault (Short)", {.head = 0, .tail = 0, .cache_case = 36, .no_cache_case = 38, .reads = 1, .writes = 10, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_BUS_FAULT_LONG] = {"Bus Cycle Fault (Long)", {.head = 0, .tail = 0, .cache_case = 62, .no_cache_case = 64, .reads = 1, .writes = 24, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_NORMAL] = {"RTE (Normal Four Word)", {.head = 1, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_SIX_WORD] = {"RTE (Six Word)", {.head = 1, .tail = 0, .cache_case = 18, .no_cache_case = 20, .reads = 4, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_THROWAWAY] = {"RTE (Throwaway)", {.head = 1, .tail = 0, .cache_case = 12, .no_cache_case = 12, .reads = 4}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_COPROCESSOR] = {"RTE (Coprocessor)", {.head = 1, .tail = 0, .cache_case = 26, .no_cache_case = 26, .reads = 7, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_SHORT_FAULT] = {"RTE (Short Fault)", {.head = 1, .tail = 0, .cache_case = 36, .no_cache_case = 36, .reads = 10, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_RTE_LONG_FAULT] = {"RTE (Long Fault)", {.head = 1, .tail = 0, .cache_case = 76, .no_cache_case = 76, .reads = 25, .prefetches = 2}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.16's bounds checks, by outcome, from the page image (p. 11-49).
     * `CHK EA,Dn` is `*`, fetch; `CHK2` is `#`, which on that page is "Add Fetch
     * Immediate Address Time" -- §11.6.2 through its extension word, whose
     * `(An)` row reads one bound while the `CHK2` row reads the other: the two
     * reads this core runs and `[020]` §9.2.16 prints. Every row but the two
     * in-bounds `CHK` rows carries `+`, "Indicates Maximum Time", and is
     * `PROVISIONAL` with the divides. The in-bounds rows are the word lookup's
     * and the selected lookup's; out of bounds is vector 6's.
     *
     * **The exception-taken rows print four writes**, where Table 8-6 puts both
     * instructions in the six-word frame and this same page's neighbours --
     * `TRAPcc (Trap)`, `TRAPV (Trap)` -- print that frame's five. Transcribed as
     * printed: their microcode is the page's less its own bus, and the frame's
     * fifth write is measured. Recorded in `M68030_WALK.md`. */
    [ROW_CHK_DN_DN] = {"CHK Dn,Dn (No Exception)", {.head = 8, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CHK_DN_DN_TAKEN] = {"CHK Dn,Dn (Exception Taken)", {.head = 4, .tail = 0, .cache_case = 28, .no_cache_case = 30, .reads = 1, .writes = 4, .prefetches = 3}, true, AP_M68030_EA_TIME_NONE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CHK_EA_DN] = {"CHK EA,Dn (No Exception)", {.head = 0, .tail = 0, .cache_case = 8, .no_cache_case = 8, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_SINGLE_WORD},
    [ROW_CHK_EA_DN_TAKEN] = {"CHK EA,Dn (Exception Taken)", {.head = 0, .tail = 0, .cache_case = 28, .no_cache_case = 30, .reads = 1, .writes = 4, .prefetches = 3}, true, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CHK2] = {"CHK2 Mem,Rn (No Exception)", {.head = 2, .tail = 0, .cache_case = 18, .no_cache_case = 18, .reads = 1, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_CHK2_TAKEN] = {"CHK2 Mem,Rn (Exception Taken)", {.head = 2, .tail = 0, .cache_case = 40, .no_cache_case = 42, .reads = 2, .writes = 4, .prefetches = 3}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.8's rows the extension word selects, from the page image
     * (p. 11-41). All `**`, "Add Fetch Immediate Effective Address Time" --
     * §11.6.2 through the extension word -- and all `+`. `CMP2` is here rather
     * than beside `CHK2` in §11.6.16, and costs two more. The long multiplies
     * have one row each whatever the source; the long divides a register row,
     * above, and this one. The 64-bit forms, extension bit 10, print no row of
     * their own. */
    [ROW_CMP2] = {"CMP2 EA,Rn", {.head = 2, .tail = 0, .cache_case = 20, .no_cache_case = 20, .reads = 1, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MULS_L_EA] = {"MULS.L EA,Dn", {.head = 2, .tail = 0, .cache_case = 44, .no_cache_case = 44, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MULU_L_EA] = {"MULU.L EA,Dn", {.head = 2, .tail = 0, .cache_case = 44, .no_cache_case = 44, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_DIVS_L_EA] = {"DIVS.L EA,Dn", {.head = 0, .tail = 0, .cache_case = 90, .no_cache_case = 90, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_DIVU_L_EA] = {"DIVU.L EA,Dn", {.head = 0, .tail = 0, .cache_case = 78, .no_cache_case = 78, .prefetches = 1}, true, AP_M68030_EA_TIME_FETCH_IMMEDIATE, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},

    /* §11.6.6's mode-6 destinations, from the page images (pp. 11-37, 11-38),
     * every one `*`, the source through §11.6.1.
     *
     * The full format prints 26 rows in two groups -- `(d16,An)` spelled out,
     * and `B` for "0, An, PC, Xn, An + Xn, PC + Xn" -- which reduce to sixteen
     * figure sets, since the page's own note says the index does not affect
     * timing and every indexed row equals its unindexed neighbour. **Every
     * group A row equals its group B row with the base displacement dropped**,
     * as in §11.6.1 and §11.6.3: a third table confirming the `PROVISIONAL`
     * reading that a word base displacement is free when the base is a register
     * (`ap_m68030_ea_timing.h`). Rows are named for the first form the page
     * prints with those figures.
     *
     * Classes count the instruction's own words and the destination's, as
     * `MOVE EA,xxx.L` does: an even count invariant, an odd one odd. */
    [ROW_MOVE_BRIEF] = {"MOVE EA,(d8,An,Xn)", {.head = 4, .tail = 0, .cache_case = 6, .no_cache_case = 7, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_A_NONE] = {"MOVE EA,(d16,An) or (d16,PC)", {.head = 2, .tail = 0, .cache_case = 8, .no_cache_case = 9, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_A_OD_NULL] = {"MOVE EA,([d16,An],Xn) or ([d16,PC],Xn)", {.head = 2, .tail = 0, .cache_case = 10, .no_cache_case = 11, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_A_OD_WORD] = {"MOVE EA,([d16,An],d16) or ([d16,PC],d16)", {.head = 2, .tail = 0, .cache_case = 12, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_A_OD_LONG] = {"MOVE EA,([d16,An],d32) or ([d16,PC],d32)", {.head = 2, .tail = 0, .cache_case = 14, .no_cache_case = 16, .reads = 1, .writes = 1, .prefetches = 3}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_NONE_BD_NULL] = {"MOVE EA,(B)", {.head = 4, .tail = 0, .cache_case = 8, .no_cache_case = 9, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_NONE_BD_WORD] = {"MOVE EA,(d16,B)", {.head = 4, .tail = 0, .cache_case = 10, .no_cache_case = 12, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_NONE_BD_LONG] = {"MOVE EA,(d32,B)", {.head = 4, .tail = 0, .cache_case = 14, .no_cache_case = 16, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_OD_NULL_BD_NULL] = {"MOVE EA,([B])", {.head = 4, .tail = 0, .cache_case = 10, .no_cache_case = 11, .reads = 1, .writes = 1, .prefetches = 1}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_OD_NULL_BD_WORD] = {"MOVE EA,([d16,B])", {.head = 4, .tail = 0, .cache_case = 12, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_OD_NULL_BD_LONG] = {"MOVE EA,([d32,B])", {.head = 4, .tail = 0, .cache_case = 16, .no_cache_case = 18, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_OD_WORD_BD_NULL] = {"MOVE EA,([B],d16)", {.head = 4, .tail = 0, .cache_case = 12, .no_cache_case = 14, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_OD_WORD_BD_WORD] = {"MOVE EA,([d16,B],d16)", {.head = 4, .tail = 0, .cache_case = 14, .no_cache_case = 17, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_OD_WORD_BD_LONG] = {"MOVE EA,([d32,B],d16)", {.head = 4, .tail = 0, .cache_case = 18, .no_cache_case = 21, .reads = 1, .writes = 1, .prefetches = 3}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_OD_LONG_BD_NULL] = {"MOVE EA,([B],d32)", {.head = 4, .tail = 0, .cache_case = 14, .no_cache_case = 16, .reads = 1, .writes = 1, .prefetches = 2}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
    [ROW_MOVE_FULL_OD_LONG_BD_WORD] = {"MOVE EA,([d16,B],d32)", {.head = 4, .tail = 0, .cache_case = 16, .no_cache_case = 19, .reads = 1, .writes = 1, .prefetches = 3}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ODD_WORDS},
    [ROW_MOVE_FULL_OD_LONG_BD_LONG] = {"MOVE EA,([d32,B],d32)", {.head = 4, .tail = 0, .cache_case = 20, .no_cache_case = 23, .reads = 1, .writes = 1, .prefetches = 3}, false, AP_M68030_EA_TIME_FETCH, AP_M68030_PREFETCH_ALIGNMENT_INVARIANT},
};

#define TABLE_COUNT (sizeof TABLE / sizeof TABLE[0])

const ap_m68030_table_entry_t *ap_m68030_timing_table(unsigned *count) {
  *count = (unsigned)TABLE_COUNT;
  return TABLE;
}


const ap_m68030_table_entry_t *
ap_m68030_timing_for_exception(ap_m68030_exception_row_t row) {
  switch (row) {
  case AP_M68030_EXCEPTION_INTERRUPT_I_STACK:
    return &TABLE[ROW_INTERRUPT_I];
  case AP_M68030_EXCEPTION_INTERRUPT_M_STACK:
    return &TABLE[ROW_INTERRUPT_M];
  case AP_M68030_EXCEPTION_TRACE:
    return &TABLE[ROW_TRACE];
  case AP_M68030_EXCEPTION_BUS_FAULT_SHORT:
    return &TABLE[ROW_BUS_FAULT_SHORT];
  case AP_M68030_EXCEPTION_BUS_FAULT_LONG:
    return &TABLE[ROW_BUS_FAULT_LONG];
  case AP_M68030_EXCEPTION_RTE_THROWAWAY:
    return &TABLE[ROW_RTE_THROWAWAY];
  }
  return nullptr;
}

const ap_m68030_table_entry_t *ap_m68030_timing_for_vector(unsigned vector,
                                                           uint16_t instruction) {
  if (vector >= AP_M68030_VECTOR_TRAP_BASE &&
      vector < AP_M68030_VECTOR_TRAP_BASE + 16u) {
    return &TABLE[ROW_TRAP_N];
  }
  switch (vector) {
  case AP_M68030_VECTOR_ILLEGAL_INSTRUCTION:
    return &TABLE[ROW_ILLEGAL];
  case AP_M68030_VECTOR_LINE_A:
    return &TABLE[ROW_LINE_A];
  case AP_M68030_VECTOR_LINE_F:
    return &TABLE[ROW_LINE_F];
  case AP_M68030_VECTOR_PRIVILEGE_VIOLATION:
    return &TABLE[ROW_PRIVILEGE];
  case AP_M68030_VECTOR_CHK:
    /* `CHK` by its bound's addressing mode, and `CHK2` -- `CMP2`'s encoding
     * with extension bit 11 set, and only `CHK2` raises the vector. */
    if ((instruction & 0xF140u) == 0x4100u) {
      return &TABLE[((instruction >> 3) & 7u) == 0u ? ROW_CHK_DN_DN_TAKEN
                                                    : ROW_CHK_EA_DN_TAKEN];
    }
    if ((instruction & 0xF9C0u) == 0x00C0u) {
      return &TABLE[ROW_CHK2_TAKEN];
    }
    return nullptr;
  case AP_M68030_VECTOR_TRAPCC:
    /* Four instructions share the vector at four costs, and only the word
     * says which. A coprocessor's `cpTRAPcc` shares it too and is not on the
     * page. */
    if (instruction == 0x4E76u) {
      return &TABLE[ROW_TRAPV_TRAP];
    }
    switch (instruction & 0xF0FFu) {
    case 0x50FAu:
      return &TABLE[ROW_TRAPCC_W_TRAP];
    case 0x50FBu:
      return &TABLE[ROW_TRAPCC_L_TRAP];
    case 0x50FCu:
      return &TABLE[ROW_TRAPCC_TRAP];
    default:
      return nullptr;
    }
  default:
    return nullptr;
  }
}

/* §11.6.7's `MOVEM`, from the page image (p. 11-39): `% + MOVEM EA,RL` is head
 * 2, `8+4n(n/0/0)` and `8+4n(n/1/0)`; `% + MOVEM RL,EA` is head 2,
 * `4+2n(0/0/n)` and `4+2n(0/1/n)`. `%` there is Calculate Immediate, §11.6.4
 * through the mask word, and `+` a maximum time, so both are `PROVISIONAL`.
 * `[020]` §9.2.7 agrees on `8+4n` and prints `4+3n` for the write, a
 * different part's microcode.
 *
 * **The footnote prices wait states separately, and this core does not follow
 * it.** It gives `EA,RL` as `8+4n` for up to two wait states and `(8+4n)+(w-2)n`
 * past that, and `RL,EA` as `(4+2n)+(n-1)w` with a tail of `(n-1)w`: some of a
 * slow cycle's wait hidden under the microcode. The step instead adds the bus
 * it measured, wait states and all, to the row's microcode -- so on a slow
 * port `MOVEM` costs up to `nw` more than the footnote. `PROVISIONAL`, and the
 * same for every row: the footnote is the only place §11.6 says how a wait
 * state overlaps. */
const ap_m68030_table_entry_t *
ap_m68030_timing_for_movem(uint16_t instruction, uint16_t mask,
                           ap_m68030_table_entry_t *storage) {
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);
  /* `0100 1d00 1s`: `d` the direction, 1 memory to registers, `s` the size.
   * Mode 0 is `EXT`. */
  if ((instruction & 0xFB80u) != 0x4880u || mode < 0x2u) {
    return nullptr;
  }
  const bool to_registers = (instruction & 0x0400u) != 0u;
  /* "only control addressing modes or the postincrement" into registers, and
   * "only control alterable addressing modes or the predecrement" out of
   * them. */
  const bool allowed =
      to_registers ? (mode == 0x2u || mode == 0x3u || mode == 0x5u ||
                      mode == 0x6u || (mode == 0x7u && reg <= 0x3u))
                   : (mode == 0x2u || mode == 0x4u || mode == 0x5u ||
                      mode == 0x6u || (mode == 0x7u && reg <= 0x1u));
  unsigned n = 0;
  for (uint16_t rest = mask; rest != 0u; rest = (uint16_t)(rest & (rest - 1u))) {
    n++;
  }
  if (!allowed || n == 0u) {
    return nullptr;
  }
  if (to_registers) {
    *storage = (ap_m68030_table_entry_t){
        "MOVEM EA,RL",
        {.head = 2, .tail = 0, .cache_case = 8u + 4u * n,
         .no_cache_case = 8u + 4u * n, .reads = n, .prefetches = 1},
        true, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE,
        AP_M68030_PREFETCH_ALIGNMENT_INVARIANT};
  } else {
    *storage = (ap_m68030_table_entry_t){
        "MOVEM RL,EA",
        {.head = 2, .tail = 0, .cache_case = 4u + 2u * n,
         .no_cache_case = 4u + 2u * n, .writes = n, .prefetches = 1},
        true, AP_M68030_EA_TIME_CALCULATE_IMMEDIATE,
        AP_M68030_PREFETCH_ALIGNMENT_INVARIANT};
  }
  return storage;
}

const ap_m68030_table_entry_t *
ap_m68030_timing_for_move_indexed(uint16_t instruction, uint16_t extension) {
  const unsigned family = (unsigned)((instruction >> 12) & 0xFu);
  if (family < 0x1u || family > 0x3u || ((instruction >> 6) & 0x7u) != 0x6u) {
    return nullptr;
  }
  const ap_m68030_extension_t decoded = ap_m68030_ea_decode_extension(extension);
  if (!decoded.full_format) {
    return &TABLE[ROW_MOVE_BRIEF];
  }
  if (decoded.reserved) {
    return nullptr;
  }
  /* Group A is a word base displacement off a register, the reading
   * `ap_m68030_ea_fetch_timing_full` makes, and the base displacement drops
   * out of its figures. */
  const bool word_based = decoded.base_displacement_size == AP_M68030_BD_WORD &&
                          !decoded.base_suppressed;
  const unsigned bd = decoded.base_displacement_size == AP_M68030_BD_NULL   ? 0u
                      : decoded.base_displacement_size == AP_M68030_BD_WORD ? 1u
                      : decoded.base_displacement_size == AP_M68030_BD_LONG ? 2u
                                                                            : 3u;
  if (bd == 3u) {
    return nullptr;
  }

  if (decoded.indirect == AP_M68030_INDIRECT_NONE) {
    if (word_based) {
      return &TABLE[ROW_MOVE_FULL_A_NONE];
    }
    static const unsigned NONE[3] = {ROW_MOVE_FULL_NONE_BD_NULL,
                                     ROW_MOVE_FULL_NONE_BD_WORD,
                                     ROW_MOVE_FULL_NONE_BD_LONG};
    return &TABLE[NONE[bd]];
  }
  if (decoded.indirect == AP_M68030_INDIRECT_RESERVED) {
    return nullptr;
  }
  switch (decoded.outer_displacement_size) {
  case AP_M68030_OD_NULL: {
    if (word_based) {
      return &TABLE[ROW_MOVE_FULL_A_OD_NULL];
    }
    static const unsigned OD_NULL[3] = {ROW_MOVE_FULL_OD_NULL_BD_NULL,
                                        ROW_MOVE_FULL_OD_NULL_BD_WORD,
                                        ROW_MOVE_FULL_OD_NULL_BD_LONG};
    return &TABLE[OD_NULL[bd]];
  }
  case AP_M68030_OD_WORD: {
    if (word_based) {
      return &TABLE[ROW_MOVE_FULL_A_OD_WORD];
    }
    static const unsigned OD_WORD[3] = {ROW_MOVE_FULL_OD_WORD_BD_NULL,
                                        ROW_MOVE_FULL_OD_WORD_BD_WORD,
                                        ROW_MOVE_FULL_OD_WORD_BD_LONG};
    return &TABLE[OD_WORD[bd]];
  }
  case AP_M68030_OD_LONG: {
    if (word_based) {
      return &TABLE[ROW_MOVE_FULL_A_OD_LONG];
    }
    static const unsigned OD_LONG[3] = {ROW_MOVE_FULL_OD_LONG_BD_NULL,
                                        ROW_MOVE_FULL_OD_LONG_BD_WORD,
                                        ROW_MOVE_FULL_OD_LONG_BD_LONG};
    return &TABLE[OD_LONG[bd]];
  }
  case AP_M68030_OD_NONE:
    break;
  }
  return nullptr;
}

const ap_m68030_table_entry_t *
ap_m68030_timing_for_selected(uint16_t instruction, uint16_t extension,
                              bool outcome) {
  const unsigned mode = (unsigned)((instruction >> 3) & 0x7u);
  const unsigned reg = (unsigned)(instruction & 0x7u);
  const bool control = mode == 0x2u || mode == 0x5u || mode == 0x6u ||
                       (mode == 0x7u && reg <= 0x3u);
  const bool control_alterable = mode == 0x2u || mode == 0x5u ||
                                 mode == 0x6u || (mode == 0x7u && reg <= 0x1u);
  const bool memory_alterable = (mode >= 0x2u && mode <= 0x6u) ||
                                (mode == 0x7u && reg <= 0x1u);

  /* `MOVEC Rn,Cr`. §11.6.7's groups are by name, and these are their codes;
   * any other code is not a 68030 register and the instruction does not run. */
  if (instruction == 0x4E7Bu) {
    const unsigned which = extension & 0x0FFFu;
    if (which <= 0x002u) {
      return &TABLE[ROW_MOVEC_RN_CR_B]; /* SFC, DFC, CACR */
    }
    if (which >= 0x800u && which <= 0x804u) {
      return &TABLE[ROW_MOVEC_RN_CR_A]; /* USP, VBR, CAAR, MSP, ISP */
    }
    return nullptr;
  }

  /* `RTE`, by the format of the frame it unstacked -- `extension` carries the
   * format number. A throwaway frame is priced on top of the one behind it, so
   * it is the last frame read that selects. */
  if (instruction == 0x4E73u) {
    switch (extension) {
    case AP_M68030_FRAME_SHORT:
      return &TABLE[ROW_RTE_NORMAL];
    case AP_M68030_FRAME_THROWAWAY:
      return &TABLE[ROW_RTE_THROWAWAY];
    case AP_M68030_FRAME_SIX_WORD:
      return &TABLE[ROW_RTE_SIX_WORD];
    case AP_M68030_FRAME_COPROCESSOR_MID:
      return &TABLE[ROW_RTE_COPROCESSOR];
    case AP_M68030_FRAME_SHORT_BUS_FAULT:
      return &TABLE[ROW_RTE_SHORT_FAULT];
    case AP_M68030_FRAME_LONG_BUS_FAULT:
      return &TABLE[ROW_RTE_LONG_FAULT];
    default:
      return nullptr;
    }
  }

  /* `CHK2` and `CMP2`, `0000 0ss0 11` with a size of `00`-`10`: extension bit
   * 11 set is `CHK2` (§11.6.16, in bounds; out of bounds is vector 6's row),
   * clear is `CMP2` (§11.6.8).
   *
   * *Corrected 2026-09-15. This read:* "Clear, it is `CMP2`, for which §11.6
   * prints no row." *§11.6.8's second page prints it; the search had stopped at
   * §11.6.16.* */
  if ((instruction & 0xF9C0u) == 0x00C0u && ((instruction >> 9) & 0x3u) != 0x3u) {
    if (!control) {
      return nullptr;
    }
    return &TABLE[(extension & 0x0800u) != 0u ? ROW_CHK2 : ROW_CMP2];
  }

  /* §11.6.8's long multiplies and divides, `0100 1100 0x`: bit 6 clear
   * multiplies and set divides, and extension bit 11 is the signed form. The
   * source is any data mode, so an address register and mode 7's unassigned
   * registers have no row. */
  if ((instruction & 0xFF80u) == 0x4C00u && mode != 0x1u &&
      !(mode == 0x7u && reg > 0x4u)) {
    const bool is_signed = (extension & 0x0800u) != 0u;
    if ((instruction & 0x0040u) == 0u) {
      return &TABLE[is_signed ? ROW_MULS_L_EA : ROW_MULU_L_EA];
    }
    if (mode == 0x0u) {
      return &TABLE[is_signed ? ROW_DIVS_L : ROW_DIVU_L];
    }
    return &TABLE[is_signed ? ROW_DIVS_L_EA : ROW_DIVU_L_EA];
  }

  /* `CAS2`, `$0CFC` and `$0EFC` -- ahead of `CAS`, whose group they sit in with
   * a mode that `CAS` itself cannot take. */
  if (instruction == 0x0CFCu || instruction == 0x0EFCu) {
    return &TABLE[outcome ? ROW_CAS2_MATCH : ROW_CAS2_MISMATCH];
  }
  /* `CAS`, `0000 1ss0 11` with a size of `01`-`11`; `00` there is `BSET #`. */
  if ((instruction & 0xF9C0u) == 0x08C0u && ((instruction >> 9) & 0x3u) != 0u) {
    if (!memory_alterable) {
      return nullptr;
    }
    return &TABLE[outcome ? ROW_CAS_MATCH : ROW_CAS_MISMATCH];
  }
  /* `MOVES`, `0000 1110 ss` with a size of `00`-`10`; `11` is `CAS.L`.
   * Extension bit 11 is the direction, 1 register to memory. */
  if ((instruction & 0xFF00u) == 0x0E00u && ((instruction >> 6) & 0x3u) != 0x3u) {
    if (!memory_alterable) {
      return nullptr;
    }
    return &TABLE[(extension & 0x0800u) != 0u ? ROW_MOVES_RN_EA
                                              : ROW_MOVES_EA_RN];
  }
  /* §11.6.14's bit fields in memory, `1110 1ttt 11`. The four that write take
   * control alterable modes, the four that only read any control mode. */
  if ((instruction & 0xF8C0u) == 0xE8C0u && mode != 0x0u) {
    const unsigned type = (unsigned)((instruction >> 8) & 7u);
    const bool writes = type == 2u || type == 4u || type == 6u || type == 7u;
    if (!(writes ? control_alterable : control)) {
      return nullptr;
    }
    static const unsigned FEWER[8] = {
        ROW_BFTST_MEM, ROW_BFEXTU_MEM, ROW_BFCHG_MEM, ROW_BFEXTS_MEM,
        ROW_BFCLR_MEM, ROW_BFFFO_MEM,  ROW_BFSET_MEM, ROW_BFINS_MEM};
    static const unsigned FIVE[8] = {
        ROW_BFTST_MEM5, ROW_BFEXTU_MEM5, ROW_BFCHG_MEM5, ROW_BFEXTS_MEM5,
        ROW_BFCLR_MEM5, ROW_BFFFO_MEM5,  ROW_BFSET_MEM5, ROW_BFINS_MEM5};
    return &TABLE[outcome ? FIVE[type] : FEWER[type]];
  }
  /* §11.6.12's register-count `LSd` and `ASR`, `1110 ccc d ss 1 tt rrr` with a
   * size of `00`-`10`, by whether the count exceeded the operand's size -- the
   * page's `%` and `+`. `ASL Dx,Dy` and `ROd Dx,Dy` print once, and `ROXd Dn`
   * once, and are the word lookup's. */
  if ((instruction & 0xF020u) == 0xE020u && ((instruction >> 6) & 0x3u) != 0x3u) {
    const unsigned type = (unsigned)((instruction >> 3) & 0x3u);
    const bool left = ((instruction >> 8) & 1u) != 0u;
    if (type == 0x1u) {
      return &TABLE[outcome ? ROW_LS_DX_BEYOND : ROW_LS_DX_WITHIN];
    }
    if (type == 0x0u && !left) {
      return &TABLE[outcome ? ROW_ASR_DX_BEYOND : ROW_ASR_DX_WITHIN];
    }
  }
  return nullptr;
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
  case 0x4E70u:
    return &TABLE[ROW_RESET];
  case 0x4E71u:
    return &TABLE[ROW_NOP];
  case 0x4E75u:
    return &TABLE[ROW_RTS];
  case 0x4E77u:
    return &TABLE[ROW_RTR];
  case 0x4E74u:
    return &TABLE[ROW_RTD];
  case 0x4E7Au:
    /* `MOVEC Cr,Rn`, one row for every control register. The other direction
     * splits by register group, which only the extension word names, so
     * `$4E7B` has no row here. */
    return &TABLE[ROW_MOVEC_CR_RN];
  case 0x4E72u:
    return &TABLE[ROW_STOP];
  case 0x4E76u:
    /* The row when V is clear. A set V traps, and that is the exception's row,
     * found by its vector. */
    return &TABLE[ROW_TRAPV_NO_TRAP];
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
  /* §11.6.7's user stack pointer pair, `$4E6x`, bit 3 the direction. */
  if ((instruction & 0xFFF8u) == 0x4E60u) {
    return &TABLE[ROW_MOVE_AN_USP];
  }
  if ((instruction & 0xFFF8u) == 0x4E68u) {
    return &TABLE[ROW_MOVE_USP_AN];
  }

  /* Family 0000: the immediates of §11.6.9, the bit operations of §11.6.13 and
   * the logical-to-status forms of §11.6.16.
   *
   * **`ADDI #<data>,Dn` and the status forms were rows nothing returned** --
   * both sat in the table from the day they were transcribed and no branch of
   * this function reached them, so every `ORI #$0700,SR` the boot PROM runs was
   * charged bus time alone. The same defect `NBCD` had, found by the same
   * question: what is the row called by? `timing_table_suite` now asks it of
   * every row. */
  if (family == 0x0u) {
    const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
    const unsigned reg = (unsigned)(instruction & 0x7u);
    const bool dynamic_bit = ((instruction >> 8) & 1u) != 0u;
    const unsigned operation = (unsigned)((instruction >> 9) & 0x7u);
    const bool immediate_destination = (mode == 0x7u) && (reg == 0x4u);

    if (dynamic_bit) {
      /* `0000 rrr1 tt mode reg`, where mode 001 is `MOVEP` instead. `tt` is
       * the operation, not a size. */
      if (mode == 0x1u) {
        /* §11.6.7's `MOVEP`: bit 7 is the direction, 0 memory to register,
         * and bit 6 the size, 0 a word. */
        static const unsigned MOVEP[4] = {
            ROW_MOVEP_W_FROM_MEM, ROW_MOVEP_L_FROM_MEM, ROW_MOVEP_W_TO_MEM,
            ROW_MOVEP_L_TO_MEM};
        return &TABLE[MOVEP[size_field]];
      }
      /* Only `BTST` may test a bit of immediate data; the other three would
       * be writing to it. */
      if (immediate_destination && size_field != 0x0u) {
        return nullptr;
      }
      static const unsigned DN_DN[4] = {ROW_BTST_DN_DN, ROW_BCHG_DN_DN,
                                        ROW_BCLR_DN_DN, ROW_BSET_DN_DN};
      static const unsigned DN_MEM[4] = {ROW_BTST_DN_MEM, ROW_BCHG_DN_MEM,
                                         ROW_BCLR_DN_MEM, ROW_BSET_DN_MEM};
      return &TABLE[mode == 0x0u ? DN_DN[size_field] : DN_MEM[size_field]];
    }

    if (operation == 0x4u) {
      /* `0000 1000 tt mode reg`, the static forms: the bit number is in an
       * extension word. */
      if (mode == 0x1u || immediate_destination) {
        return nullptr;
      }
      static const unsigned IMM_DN[4] = {ROW_BTST_IMM_DN, ROW_BCHG_IMM_DN,
                                         ROW_BCLR_IMM_DN, ROW_BSET_IMM_DN};
      static const unsigned IMM_MEM[4] = {ROW_BTST_IMM_MEM, ROW_BCHG_IMM_MEM,
                                          ROW_BCLR_IMM_MEM, ROW_BSET_IMM_MEM};
      return &TABLE[mode == 0x0u ? IMM_DN[size_field] : IMM_MEM[size_field]];
    }

    /* A size of `11` is `CMP2`/`CHK2`/`CALLM`/`RTM`, and operation 111 is
     * `MOVES` and `CAS` -- different instructions, not wider immediates. */
    if (size_field == 0x3u || operation == 0x7u || mode == 0x1u) {
      return nullptr;
    }

    if (immediate_destination) {
      /* `ORI`, `ANDI` and `EORI` to `CCR` (size 00) or `SR` (size 01). */
      const bool logical =
          operation == 0x0u || operation == 0x1u || operation == 0x5u;
      if (logical && size_field <= 0x1u) {
        return &TABLE[ROW_LOGICAL_TO_SR];
      }
      return nullptr;
    }

    static const unsigned IMMEDIATE_DN[8] = {ROW_ORI_DN,  ROW_ANDI_DN,
                                             ROW_SUBI_DN, ROW_ADDI_DN,
                                             ROW_COUNT,   ROW_EORI_DN,
                                             ROW_CMPI_DN, ROW_COUNT};
    static const unsigned IMMEDIATE_MEM[8] = {ROW_ORI_MEM,  ROW_ANDI_MEM,
                                              ROW_SUBI_MEM, ROW_ADDI_MEM,
                                              ROW_COUNT,    ROW_EORI_MEM,
                                              ROW_CMPI_MEM, ROW_COUNT};
    const unsigned row =
        mode == 0x0u ? IMMEDIATE_DN[operation] : IMMEDIATE_MEM[operation];
    return row == ROW_COUNT ? nullptr : &TABLE[row];
  }

  /* `ADDQ` and `SUBQ` to memory, §11.6.9's `*` rows. Ahead of the
   * memory-destination block below, which reads bits 8-6 as an arithmetic
   * opmode and would send a `SUBQ` to no row. A size of `11` is `Scc`/`DBcc`/
   * `TRAPcc`, which are not these. */
  /* `TRAPcc`, the `Scc` group's mode 7 registers 2-4: a word operand, a long,
   * or none. These are the rows when the condition is false; a trap is the
   * exception's row, found by its vector. */
  if (family == 0x5u && ((instruction >> 6) & 0x3u) == 0x3u && mode == 0x7u) {
    switch (instruction & 0x7u) {
    case 0x2u:
      return &TABLE[ROW_TRAPCC_W_NO_TRAP];
    case 0x3u:
      return &TABLE[ROW_TRAPCC_L_NO_TRAP];
    case 0x4u:
      return &TABLE[ROW_TRAPCC_NO_TRAP];
    default:
      break;
    }
  }
  /* `Scc Dn`, the other row nothing returned: size `11` with mode 000. Mode
   * 001 is `DBcc`, priced through `ap_m68030_timing_for_dbcc`. */
  if (family == 0x5u && ((instruction >> 6) & 0x3u) == 0x3u && mode == 0x0u) {
    return &TABLE[ROW_SCC_DN];
  }
  /* `Scc Mem`, §11.6.11's `**` row. Mode 7 registers 2-4 in this group are
   * `TRAPcc` with its word, long and no operand, not a set of an immediate. */
  if (family == 0x5u && ((instruction >> 6) & 0x3u) == 0x3u && mode >= 0x2u &&
      !(mode == 0x7u && (instruction & 0x7u) >= 0x2u)) {
    return &TABLE[ROW_SCC_MEM];
  }
  if (family == 0x5u && ((instruction >> 6) & 0x3u) != 0x3u &&
      !register_source) {
    const bool subtract = ((instruction >> 8) & 1u) != 0u;
    return subtract ? &TABLE[ROW_SUBQ_MEM] : &TABLE[ROW_ADDQ_MEM];
  }

  /* §11.6.11's single-operand forms, family 0100. Bits 11-9 choose the
   * operation and bits 7-6 the size, with `11` an escape to a different
   * instruction entirely -- so a size of `11` is not a wider operand here and
   * is refused rather than mapped. Only the data-register form is transcribed;
   * the memory forms carry a `*` or `**` effective address time. */
  if (family == 0x4u) {
    const unsigned row = (unsigned)((instruction >> 9) & 0x7u);
    const unsigned size_field = (unsigned)((instruction >> 6) & 0x3u);
    const unsigned ea_register = (unsigned)(instruction & 0x7u);

    /* §11.6.16's address forms: `JMP` and `JSR` through §11.6.5, `LEA` and
     * `PEA` through §11.6.3. Only the control modes are instructions -- `(An)`,
     * `(d16,An)`, mode 6, the absolutes and the two PC-relative forms. */
    const bool control_mode = mode == 0x2u || mode == 0x5u || mode == 0x6u ||
                              (mode == 0x7u && ea_register <= 0x3u);
    if (control_mode) {
      if ((instruction & 0xFFC0u) == 0x4EC0u) {
        return &TABLE[ROW_JMP];
      }
      if ((instruction & 0xFFC0u) == 0x4E80u) {
        return &TABLE[ROW_JSR];
      }
      if ((instruction & 0xF1C0u) == 0x41C0u) {
        return &TABLE[ROW_LEA];
      }
      if ((instruction & 0xFFC0u) == 0x4840u) {
        return &TABLE[ROW_PEA];
      }
    }
    /* §11.6.7's forms in this family. `SWAP` is `PEA`'s group with a data
     * register, and the four status moves take the size field's `11`. */
    if ((instruction & 0xFFF8u) == 0x4840u) {
      return &TABLE[ROW_SWAP];
    }
    /* And `BKPT` is the same group with an address register: the breakpoint
     * acknowledge cycle. One that nothing answers becomes an illegal
     * instruction, priced by that exception's row as well. */
    if ((instruction & 0xFFF8u) == 0x4848u) {
      return &TABLE[ROW_BKPT];
    }
    /* §11.6.16's `CHK`, `0100 rrr1 s0`: bit 8 set and bit 6 clear, `s` of 11 a
     * word and 10 a long. These are its in-bounds rows; out of bounds is the
     * exception's row, by vector 6. The bound is a data operand, so an address
     * register is not one. */
    if ((instruction & 0xF140u) == 0x4100u && mode != 0x1u &&
        !(mode == 0x7u && ea_register > 0x4u)) {
      return &TABLE[mode == 0x0u ? ROW_CHK_DN_DN : ROW_CHK_EA_DN];
    }
    const bool data_alterable = mode == 0x0u || (mode >= 0x2u && mode <= 0x6u) ||
                                (mode == 0x7u && ea_register <= 0x1u);
    const bool data_source =
        mode != 0x1u && !(mode == 0x7u && ea_register > 0x4u);
    switch (instruction & 0xFFC0u) {
    case 0x40C0u: /* MOVE SR,<ea> */
      if (!data_alterable) {
        return nullptr;
      }
      return &TABLE[mode == 0x0u ? ROW_MOVE_SR_DN : ROW_MOVE_SR_MEM];
    case 0x42C0u: /* MOVE CCR,<ea> */
      if (!data_alterable) {
        return nullptr;
      }
      return &TABLE[mode == 0x0u ? ROW_MOVE_CCR_DN : ROW_MOVE_CCR_MEM];
    case 0x44C0u: /* MOVE <ea>,CCR */
      if (!data_source) {
        return nullptr;
      }
      return &TABLE[mode == 0x0u ? ROW_MOVE_DN_CCR : ROW_MOVE_EA_CCR];
    case 0x46C0u: /* MOVE <ea>,SR -- the table has no separate `Dn,SR` row */
      return data_source ? &TABLE[ROW_MOVE_EA_SR] : nullptr;
    default:
      break;
    }

    /* **Bit 8 set is none of §11.6.11's operations.** `NEGX`, `CLR`, `NEG`,
     * `NOT`, `TST` and `NBCD` all have it clear; with it set, bits 8-6 are
     * `CHK`'s `1s0` or `LEA`'s `111`. The blocks below read bits 11-9 and 7-6
     * only, so until 2026-09-14 `CHK.W (A0),D0` came back as `NEGX Mem` and
     * `CHK.L D0,D1` as `CLR Dn` -- the same trap §11.6.10's instructions had.
     * `EXTB.L`, the one bit-8 form with a row, is matched by its whole word. */
    const bool single_operand_group = ((instruction >> 8) & 1u) == 0u;
    /* Two rows this block used to refuse before reaching: `EXT` is `$488x`
     * (word), `$48Cx` (long) and `$49Cx` (`EXTB.L`), and `TAS Dn` is `$4ACx`
     * -- the size field reads `11` in three of the four, which the refusal
     * below treats as an escape. Both rows sat in the table returned by
     * nothing. */
    /* §11.6.11's memory forms: the same operations with an address, mode 2
     * and up. `TAS` is `$4AC0` with a size of `11`; `$4AFC` inside it is
     * `ILLEGAL`, mode 7 register 4, which is not a `TAS` of an immediate. Row
     * 4 with a memory operand is `NBCD Mem`, which the table does not print. */
    if (mode >= 0x2u) {
      if ((instruction & 0xFFC0u) == 0x4AC0u) {
        return instruction == 0x4AFCu ? nullptr : &TABLE[ROW_TAS_MEM];
      }
      if (size_field != 0x3u && single_operand_group) {
        switch (row) {
        case 0x0u:
          return &TABLE[ROW_NEGX_MEM];
        case 0x1u:
          return &TABLE[ROW_CLR_MEM];
        case 0x2u:
          return &TABLE[ROW_NEG_MEM];
        case 0x3u:
          return &TABLE[ROW_NOT_MEM];
        case 0x5u:
          return &TABLE[ROW_TST_MEM];
        default:
          break;
        }
      }
    }
    if (mode == 0x0u) {
      if ((instruction & 0xFFB8u) == 0x4880u ||
          (instruction & 0xFFF8u) == 0x49C0u) {
        return &TABLE[ROW_EXT_DN];
      }
      if ((instruction & 0xFFF8u) == 0x4AC0u) {
        return &TABLE[ROW_TAS_DN];
      }
    }
    if (size_field == 0x3u || mode != 0x0u || !single_operand_group) {
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
    case 0x4u:
      /* `$4800`-`$4807` is `NBCD Dn`. Bits 11-9 of `100` are shared with
       * instructions that are not wider operands of it -- a size field of `01`
       * is `SWAP`/`PEA` and `10` is `EXT`/`MOVEM` -- so only the `00` form is
       * this row, and the rest fall through to no published time rather than
       * borrowing one.
       *
       * The row existed in the table from the start and **nothing ever
       * returned it**, so `NBCD` was charged no time at all. That is not a
       * rounding error: the boot PROM's delay service is 500,000 iterations of
       * fifteen `NBCD.B`, calibrated so that its microsecond argument comes out
       * right at 25 MHz. Uncharged, a two-second delay took 0.16 s here, and
       * the loaded diagnostic's calendar test -- which reads the seconds
       * register, delays two seconds and requires it to have changed -- could
       * not pass. */
      if (size_field != 0x0u) {
        return nullptr;
      }
      return &TABLE[ROW_NBCD_DN];
    case 0x5u:
      return &TABLE[ROW_TST_DN];
    default:
      return nullptr;
    }
  }

  /* §11.6.12's shifts, family 1110. Bits 4-3 name the type, bit 8 the
   * direction and bit 5 whether the count is immediate or in a register. The
   * register-count `LSd` and `ASR` are marked `%` and `+` for counts within and
   * beyond the operand size, so the word alone cannot choose: they are
   * `ap_m68030_timing_for_selected`'s, from the count the run shifted. */
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
      /* The memory shifts, by one bit: bits 10-9 are the type and bit 8 the
       * direction. **Bit 11 set is not a shift at all** -- `$E8C0` upward are
       * the bit-field instructions -- so only bit 11 clear reaches a row. */
      if ((instruction & 0x0800u) != 0u) {
        /* §11.6.14's bit fields. Only the data register form is fixed by the
         * word: a memory form's row depends on whether the field spans five
         * bytes, which `ap_m68030_timing_for_selected` takes from the run. Bits
         * 10-8 name the instruction. */
        if (mode != 0x0u) {
          return nullptr;
        }
        static const unsigned BITFIELD_DN[8] = {
            ROW_BFTST_DN, ROW_BFEXTU_DN, ROW_BFCHG_DN, ROW_BFEXTS_DN,
            ROW_BFCLR_DN, ROW_BFFFO_DN,  ROW_BFSET_DN, ROW_BFINS_DN};
        return &TABLE[BITFIELD_DN[(instruction >> 8) & 7u]];
      }
      const bool memory_left = ((instruction >> 8) & 1u) != 0u;
      switch ((instruction >> 9) & 0x3u) {
      case 0x0u:
        return memory_left ? &TABLE[ROW_ASL_MEM] : &TABLE[ROW_ASR_MEM];
      case 0x1u:
        return &TABLE[ROW_LS_MEM];
      case 0x2u:
        return &TABLE[ROW_ROX_MEM];
      default:
        return &TABLE[ROW_RO_MEM];
      }
    }
    const unsigned type = (unsigned)((instruction >> 3) & 0x3u);
    const bool count_in_register = ((instruction >> 5) & 1u) != 0u;
    const bool left = ((instruction >> 8) & 1u) != 0u;
    if (count_in_register) {
      /* Only `LSd` and `ASR` are count-dependent -- they carry `%` and `+`.
       * `ASL Dx,Dy` and `ROd Dx,Dy` are printed once, unmarked, and `ROXd`'s
       * one row is written `Dn` with no count at all. */
      switch (type) {
      case 0x0u:
        return left ? &TABLE[ROW_ASL_DX] : nullptr;
      case 0x2u:
        return &TABLE[ROW_ROX_DN];
      case 0x3u:
        return &TABLE[ROW_RO_DX];
      default:
        return nullptr; /* LSd: by count, the selected lookup's */
      }
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
   * the field order that has caught this project out before.
   *
   * A register source into a register or a simple indirect destination has
   * its own row. Every other combination is one of §11.6.6's `*` rows, priced
   * with §11.6.1's fetch time for the source -- which the comment here used to
   * say "this module does not carry", true when it was written and not since
   * §11.6.1 landed. */
  if (family >= 0x1u && family <= 0x3u) {
    const unsigned destination_mode = (unsigned)((instruction >> 6) & 0x7u);
    const unsigned destination_register =
        (unsigned)((instruction >> 9) & 0x7u);
    if (register_source) {
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
        break;
      }
    }
    switch (destination_mode) {
    case 0x0u:
      return &TABLE[ROW_MOVE_EA_DN];
    case 0x1u:
      return &TABLE[ROW_MOVE_EA_AN];
    case 0x2u:
      return &TABLE[ROW_MOVE_SOURCE_IND];
    case 0x3u:
      return &TABLE[ROW_MOVE_SOURCE_POSTINC];
    case 0x4u:
      return &TABLE[ROW_MOVE_SOURCE_PREDEC];
    case 0x5u:
      return &TABLE[ROW_MOVE_EA_D16_AN];
    case 0x7u:
      if (destination_register == 0x0u) {
        return &TABLE[ROW_MOVE_EA_ABS_W];
      }
      if (destination_register == 0x1u) {
        return &TABLE[ROW_MOVE_EA_ABS_L];
      }
      return nullptr;
    default:
      /* Mode 6: brief or full format, told apart only by the extension. */
      return nullptr;
    }
  }

  /* MOVEQ is family 0111 with bit 8 clear, and takes no operand at all. */
  if (family == 0x7u && ((instruction >> 8) & 1u) == 0u) {
    return &TABLE[ROW_MOVEQ];
  }

  /* §11.6.10's instructions, decoded before the arithmetic they share bits
   * with. Bit 3 is the operand form: 0 a data register pair, 1 predecrement.
   * A size of `11` in `ADDX`/`SUBX`/`CMPM`'s field is the address form of the
   * arithmetic instead. */
  {
    const bool predecrement = ((instruction >> 3) & 1u) != 0u;
    const bool sized = ((instruction >> 6) & 0x3u) != 0x3u;
    /* §11.6.7's `EXG`: `$C140` a data pair, `$C148` an address pair, `$C188`
     * one of each. It is `AND`'s register-to-memory direction with a register
     * where the memory would be, and until 2026-09-14 it came back as
     * `AND Dn,Dn`. */
    if ((instruction & 0xF1F8u) == 0xC140u ||
        (instruction & 0xF1F8u) == 0xC148u ||
        (instruction & 0xF1F8u) == 0xC188u) {
      return &TABLE[ROW_EXG];
    }
    if ((instruction & 0xF1F0u) == 0xC100u) {
      return &TABLE[predecrement ? ROW_ABCD_PREDEC : ROW_ABCD_DN];
    }
    if ((instruction & 0xF1F0u) == 0x8100u) {
      return &TABLE[predecrement ? ROW_SBCD_PREDEC : ROW_SBCD_DN];
    }
    if ((instruction & 0xF1F0u) == 0x8140u) {
      return &TABLE[predecrement ? ROW_PACK_PREDEC : ROW_PACK_DN];
    }
    if ((instruction & 0xF1F0u) == 0x8180u) {
      return &TABLE[predecrement ? ROW_UNPK_PREDEC : ROW_UNPK_DN];
    }
    if ((instruction & 0xF130u) == 0xD100u && sized) {
      return &TABLE[predecrement ? ROW_ADDX_PREDEC : ROW_ADDX_DN];
    }
    if ((instruction & 0xF130u) == 0x9100u && sized) {
      return &TABLE[predecrement ? ROW_SUBX_PREDEC : ROW_SUBX_DN];
    }
    if ((instruction & 0xF138u) == 0xB108u && sized) {
      return &TABLE[ROW_CMPM];
    }
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
    /* §11.6.8's memory-source rows: the register direction with an operand
     * read from memory. Opmode 011/111 is the wide form, whose meaning is the
     * family's -- `ADDA`/`SUBA`/`CMPA`, or the word `MULU`/`MULS` and
     * `DIVU`/`DIVS`. */
    const bool wide = (opmode == 0x3u) || (opmode == 0x7u);
    const bool wide_long = (opmode == 0x7u);
    switch (family) {
    case 0xDu:
      return &TABLE[wide ? (wide_long ? ROW_ADDA_L_EA : ROW_ADDA_W_EA)
                         : ROW_ADD_EA_DN];
    case 0x9u:
      return &TABLE[wide ? (wide_long ? ROW_SUBA_L_EA : ROW_SUBA_W_EA)
                         : ROW_SUB_EA_DN];
    case 0xBu:
      return &TABLE[wide ? ROW_CMPA_EA_AN : ROW_CMP_EA_DN];
    case 0xCu:
      return &TABLE[wide ? (wide_long ? ROW_MULS_W_EA : ROW_MULU_W_EA)
                         : ROW_AND_EA_DN];
    case 0x8u:
      return &TABLE[wide ? (wide_long ? ROW_DIVS_W_EA : ROW_DIVU_W_EA)
                         : ROW_OR_EA_DN];
    default:
      return nullptr;
    }
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

  case 0xCu: /* AND, and the wide forms MULU/MULS */
    if (address_form) {
      /* §11.6.8 gives the word multiplies only as `EA,Dn`, so a register
       * operand is that row composed with §11.6.1's register row. */
      return long_size ? &TABLE[ROW_MULS_W_EA] : &TABLE[ROW_MULU_W_EA];
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
