/* Table 8-3 and Tables 8-16/8-17, transcribed. See the header for what half of
 * an instruction these numbers are and why. */

#include <stddef.h>

#include "cpu/m68882/ap_m68882_timing.h"

/* `[881]` Table 8-3, *MC68882 Overall Execution Times*, the `FPn to FPm`
 * column's **Total**, `H` and `T`. Read off the page image at 300 dpi, row by
 * row; the `H` and `T` columns beside the total are §8.5.1.3's concurrency.
 *
 * Four rows of that table are not here and each for a stated reason:
 * `FMOVE to memory` has no register-to-register form (the column prints an
 * em dash) and is `ap_m68882_store_clocks` below; `FMOVECR` reads the constant
 * ROM rather than a register, and its 32 is in the table anyway; and the two
 * `FMOVE to FPn` rows differ by Table 8-3's footnote ** -- 21 either way for
 * the total, so the distinction costs nothing here. */
typedef struct {
  ap_m68882_operation_t operation;
  unsigned clocks;
  /* Table 8-3's `H` and `T` for the same column. `H` is 17 for every arithmetic
   * operation, which is why it is carried per row rather than as a constant:
   * `FMOVE to FPn` is 21 and `FMOVECR` 10, and a constant would have to be
   * special-cased at exactly the two rows the concurrency model cares about.
   *
   * `tail == 0` marks Table 8-3's `*` -- the fully-concurrent rows, which "do
   * not have a tail time". No arithmetic row has a tail of zero, so the
   * encoding is unambiguous; `ap_m68882_operation_concurrency` turns it back
   * into the `has_tail` flag the model uses, because a zero tail and no tail
   * are different claims. */
  unsigned head;
  unsigned tail;
} operation_time_t;

static const operation_time_t OPERATION_TIMES[] = {
    /*                          Total   H    T   */
    {AP_M68882_OP_FABS,          38,   17,  17},
    {AP_M68882_OP_FACOS,        628,   17, 607},
    {AP_M68882_OP_FADD,          56,   17,  35},
    {AP_M68882_OP_FASIN,        584,   17, 563},
    {AP_M68882_OP_FATAN,        406,   17, 385},
    {AP_M68882_OP_FATANH,       696,   17, 675},
    {AP_M68882_OP_FCMP,          38,   17,  17},
    {AP_M68882_OP_FCOS,         394,   17, 373},
    {AP_M68882_OP_FCOSH,        610,   17, 589},
    {AP_M68882_OP_FDIV,         108,   17,  87},
    {AP_M68882_OP_FETOX,        500,   17, 479},
    {AP_M68882_OP_FETOXM1,      548,   17, 527},
    {AP_M68882_OP_FGETEXP,       48,   17,  27},
    {AP_M68882_OP_FGETMAN,       34,   17,  13},
    {AP_M68882_OP_FINT,          58,   17,  37},
    {AP_M68882_OP_FINTRZ,        58,   17,  37},
    {AP_M68882_OP_FLOGN,        528,   17, 507},
    {AP_M68882_OP_FLOGNP1,      574,   17, 553},
    {AP_M68882_OP_FLOG10,       584,   17, 563},
    {AP_M68882_OP_FLOG2,        584,   17, 563},
    {AP_M68882_OP_FMOD,          75,   17,  54},
    /* The one fully-concurrent row in this column: Table 8-3 prints `T = *`,
     * and Table 5-5 marks `FMOVE` the only fully-concurrent instruction. */
    {AP_M68882_OP_FMOVE_TO_FPN,  21,   21,   0},
    {AP_M68882_OP_FMUL,          76,   17,  55},
    {AP_M68882_OP_FNEG,          38,   17,  17},
    {AP_M68882_OP_FREM,         105,   17,  84},
    {AP_M68882_OP_FSCALE,        46,   17,  25},
    {AP_M68882_OP_FSGLDIV,       74,   17,  53},
    {AP_M68882_OP_FSGLMUL,       64,   17,  43},
    {AP_M68882_OP_FSIN,         394,   17, 373},
    {AP_M68882_OP_FSINCOS,      454,   17, 433},
    {AP_M68882_OP_FSINH,        690,   17, 669},
    {AP_M68882_OP_FSQRT,        110,   17,  89},
    {AP_M68882_OP_FSUB,          56,   17,  35},
    {AP_M68882_OP_FTAN,         476,   17, 455},
    {AP_M68882_OP_FTANH,        664,   17, 643},
    {AP_M68882_OP_FTENTOX,      570,   17, 549},
    {AP_M68882_OP_FTST,          36,   17,  15},
    {AP_M68882_OP_FTWOTOX,      570,   17, 549},
};

unsigned ap_m68882_operation_clocks(ap_m68882_operation_t operation) {
  /* `FSINCOS` occupies eight encodings, `$30`-`$37`, whose low three bits name
   * the second destination register. The decode already normalises them to
   * `$30`, so this sees one value and the table holds one row. */
  for (unsigned i = 0;
       i < sizeof(OPERATION_TIMES) / sizeof(OPERATION_TIMES[0]); i++) {
    if (OPERATION_TIMES[i].operation == operation) {
      return OPERATION_TIMES[i].clocks;
    }
  }
  return 0u;
}

ap_m68882_concurrency_t
ap_m68882_operation_concurrency(ap_m68882_operation_t operation) {
  for (unsigned i = 0;
       i < sizeof(OPERATION_TIMES) / sizeof(OPERATION_TIMES[0]); i++) {
    if (OPERATION_TIMES[i].operation == operation) {
      return (ap_m68882_concurrency_t){
          .head = OPERATION_TIMES[i].head,
          .tail = OPERATION_TIMES[i].tail,
          /* Table 8-3's `*`, restored from the zero the table encodes it as. */
          .has_tail = OPERATION_TIMES[i].tail != 0u,
      };
    }
  }
  return (ap_m68882_concurrency_t){0};
}

ap_m68882_overlap_state_t ap_m68882_overlap_begin(void) {
  return (ap_m68882_overlap_state_t){0};
}

/* §8.5.1.3's fourth column: "the actual overlap time, which is the lesser of
 * the effective tail and the effective head".
 *
 * One function, called from both the mid-sequence settle and the end-of-
 * sequence one. It was written out twice at first, and the duplicate is what a
 * probe found: neutering one copy left every test passing, because the case
 * that exercises the *head* bound happened to run through the other. Two copies
 * of a rule are two places for it to drift, and here they were also two places
 * a test had to reach to be worth anything. */
static uint64_t lesser(unsigned tail, unsigned head) {
  return tail < head ? tail : head;
}

static uint64_t settle(ap_m68882_overlap_state_t *state,
                       unsigned effective_head) {
  if (!state->has_pending_tail) {
    return 0u;
  }
  const unsigned tail = state->pending_tail;
  state->has_pending_tail = false;
  state->pending_tail = 0u;
  return lesser(tail, effective_head);
}

unsigned ap_m68882_overlap_add(ap_m68882_overlap_state_t *state,
                               unsigned total, unsigned head, unsigned tail,
                               bool has_tail) {
  if (state == NULL) {
    return 0u;
  }

  state->total += total;

  if (!has_tail) {
    /* "Where T is shown as `T = *`, the effective head time is the sum of the
     * FMOVE H time plus the H time of the **subsequent** instruction."
     *
     * So nothing can be settled yet: this instruction's effective head is not
     * known until the next one arrives, and the tail waiting from before it
     * has to keep waiting. Settling here against this head alone is the
     * mistake the rule exists to prevent -- on Table 8-5 it turns
     * `min(58, 44 + 42)` into `min(58, 44)` and loses 14 clocks per pair.
     *
     * Accumulated rather than assigned, so a run of consecutive no-tail
     * instructions keeps merging: the rule names "the subsequent instruction"
     * and says nothing about stopping at one. */
    state->deferred_head += head;
    state->has_deferred_head = true;
    return 0u;
  }

  /* This instruction has a tail, so it is the "subsequent instruction" any
   * deferred head was waiting for, and the pair can be settled. */
  const unsigned effective_head =
      (state->has_deferred_head ? state->deferred_head : 0u) + head;
  state->has_deferred_head = false;
  state->deferred_head = 0u;

  const uint64_t overlap = settle(state, effective_head);
  state->total -= overlap;
  state->pending_tail = tail;
  state->has_pending_tail = true;
  return (unsigned)overlap;
}

unsigned ap_m68882_overlap_flush(ap_m68882_overlap_state_t *state) {
  if (state == NULL) {
    return 0u;
  }
  unsigned overlap = 0u;
  if (state->has_deferred_head && state->has_pending_tail) {
    overlap = (unsigned)lesser(state->pending_tail, state->deferred_head);
  }
  *state = (ap_m68882_overlap_state_t){0};
  return overlap;
}

uint64_t ap_m68882_overlap_total(const ap_m68882_overlap_state_t *state) {
  if (state == NULL) {
    return 0u;
  }
  uint64_t total = state->total;
  if (state->has_deferred_head && state->has_pending_tail) {
    /* A no-tail instruction ended the sequence, so there is no subsequent head
     * to add: its effective head is its own. Table 8-5's last row is exactly
     * this -- `min(38, 21) = 21`, where the head is the smaller and a model
     * that always took the tail would answer 38 and total 348 rather than
     * 331. */
    total -= lesser(state->pending_tail, state->deferred_head);
  }
  return total;
}

ap_m68882_concurrency_t ap_m68882_store_concurrency(
    ap_m68882_format_t format) {
  switch (format) {
  case AP_M68882_FORMAT_SINGLE:
    return (ap_m68882_concurrency_t){.head = 38u, .has_tail = false};
  case AP_M68882_FORMAT_DOUBLE:
    return (ap_m68882_concurrency_t){.head = 44u, .has_tail = false};
  case AP_M68882_FORMAT_EXTENDED:
    return (ap_m68882_concurrency_t){.head = 50u, .has_tail = false};
  case AP_M68882_FORMAT_BYTE:
  case AP_M68882_FORMAT_WORD:
  case AP_M68882_FORMAT_LONG:
  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    /* `0/0` in both columns: a genuine zero head and a genuine zero tail, not
     * the `*` the three real formats carry. An integer or packed store has
     * nothing the next instruction can start inside. */
    return (ap_m68882_concurrency_t){.head = 0u, .tail = 0u, .has_tail = true};
  }
  return (ap_m68882_concurrency_t){0};
}

unsigned ap_m68882_store_clocks(ap_m68882_format_t format) {
  switch (format) {
  /* Table 8-16, *Output Operand Conversion*, the normalized-source column. */
  case AP_M68882_FORMAT_BYTE:
  case AP_M68882_FORMAT_WORD:
  case AP_M68882_FORMAT_LONG:
    /* "Integer, No Overflow", 50 for a positive source and 52 for a negative.
     * The lower of the two, because Table 8-3's row for an integer destination
     * is 110 and it is the positive case the tables assume throughout. */
    return 50u;
  case AP_M68882_FORMAT_SINGLE:
  case AP_M68882_FORMAT_DOUBLE:
    /* Table 8-16 sends both to Table 8-17, whose first row -- "No Underflow,
     * Overflow or Round Overflow", normalized source -- is 38. */
    return 38u;
  case AP_M68882_FORMAT_EXTENDED:
    /* The destination is the internal format, so there is nothing to convert
     * and 18 is the whole of it. This is the store `ap_m68882_store.c` calls
     * "the one store that cannot be inexact", and the table agrees: it is the
     * only real destination with a single number rather than a sub-table. */
    return 18u;
  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    /* Table 8-16's note 2: "1942 clocks is the typical time required for the
     * conversion, if no overflow occurs. The maximum time is 3674 clocks."
     * Two orders of magnitude above every other format, and the reason
     * Table 8-3's packed column runs to 2006 where its extended column is 50.
     * The typical value, as everywhere else here. */
    return 1942u;
  }
  return 0u;
}
