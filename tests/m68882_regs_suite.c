/* MC68882 programming model.
 *
 * A transcription cannot be checked by re-reading it, so these check it against
 * the manual's *own* tables and equations: Table 2-1's eight condition-code
 * rows, the IEEE condition derivations, and the five accrued-exception
 * equations. Where the manual states a relationship, that relationship is what
 * is asserted rather than the constants it produces.
 */

#include "cpu/m68882/ap_m68882_regs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Reset leaves the data registers as NANs, not zeros. A zeroed register reads
 * as +0, which is a perfectly good operand -- so a program that forgot to load
 * one would produce plausible answers instead of propagating a NAN. The
 * distinction only shows up on a machine that resets and then reads. */
static void test_reset_leaves_nans_rather_than_zeros(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);

  for (unsigned i = 0; i < AP_M68882_DATA_REGISTERS; i++) {
    /* An exponent of all ones with a non-zero mantissa is a NAN. */
    TEST_ASSERT_EQUAL_HEX16(0x7FFFu, regs.fp[i].exponent);
    TEST_ASSERT_TRUE(regs.fp[i].mantissa != 0u);
  }

  /* "The reset function ... clears the FPSR", and a zero mode byte "selects the
   * IEEE defaults" -- round to nearest, extended precision. */
  TEST_ASSERT_EQUAL_HEX32(0u, regs.fpsr);
  TEST_ASSERT_EQUAL_INT(AP_M68882_ROUND_NEAREST,
                        ap_m68882_rounding_mode(&regs));
  TEST_ASSERT_EQUAL_INT(AP_M68882_PRECISION_EXTENDED,
                        ap_m68882_rounding_precision(&regs));
}

/* The mode control byte's two fields, each at its own bits: rounding mode at
 * 5-4 and precision at 7-6. Swapping them is the transcription error to expect,
 * and it would leave both readable and both wrong. */
static void test_the_mode_byte_fields_are_at_their_own_bits(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);

  const ap_m68882_rounding_t modes[] = {
      AP_M68882_ROUND_NEAREST, AP_M68882_ROUND_ZERO,
      AP_M68882_ROUND_MINUS_INFINITY, AP_M68882_ROUND_PLUS_INFINITY};
  for (unsigned i = 0; i < 4u; i++) {
    regs.fpcr = (uint32_t)i << 4;
    TEST_ASSERT_EQUAL_INT(modes[i], ap_m68882_rounding_mode(&regs));
    /* And the precision field is untouched by it. */
    TEST_ASSERT_EQUAL_INT(AP_M68882_PRECISION_EXTENDED,
                          ap_m68882_rounding_precision(&regs));
  }

  const ap_m68882_precision_t precisions[] = {
      AP_M68882_PRECISION_EXTENDED, AP_M68882_PRECISION_SINGLE,
      AP_M68882_PRECISION_DOUBLE, AP_M68882_PRECISION_RESERVED};
  for (unsigned i = 0; i < 4u; i++) {
    regs.fpcr = (uint32_t)i << 6;
    TEST_ASSERT_EQUAL_INT(precisions[i], ap_m68882_rounding_precision(&regs));
    TEST_ASSERT_EQUAL_INT(AP_M68882_ROUND_NEAREST,
                          ap_m68882_rounding_mode(&regs));
  }
}

/* "11 (UNDEFINED, RESERVED)" is carried as itself rather than folded into one
 * of the three real precisions. A program selecting it is then visible, where a
 * model that returned double would run it and silently produce different
 * results from the hardware. */
static void test_the_reserved_precision_is_reported_as_reserved(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  regs.fpcr = UINT32_C(3) << 6;
  TEST_ASSERT_EQUAL_INT(AP_M68882_PRECISION_RESERVED,
                        ap_m68882_rounding_precision(&regs));
}

/* **Table 2-1 in full**: eight rows, and the manual is explicit that they are
 * the only eight the part generates -- "because of the mutually exclusive
 * nature of the data types described by the condition code bits, the FPCP
 * generates only eight of the 16 possible combinations". Setting the bits
 * through a result *kind* is what keeps the other eight unreachable. */
static void test_table_2_1_condition_codes_in_full(void) {
  const struct {
    ap_m68882_result_t kind;
    bool negative;
    uint32_t expected; /* the four condition bits, N Z I NAN */
    const char *what;
  } ROWS[] = {
      {AP_M68882_RESULT_NORMAL, false, 0u, "+normalized"},
      {AP_M68882_RESULT_NORMAL, true, 1u << AP_M68882_FPCC_N, "-normalized"},
      {AP_M68882_RESULT_ZERO, false, 1u << AP_M68882_FPCC_Z, "+0"},
      {AP_M68882_RESULT_ZERO, true,
       (1u << AP_M68882_FPCC_N) | (1u << AP_M68882_FPCC_Z), "-0"},
      {AP_M68882_RESULT_INFINITY, false, 1u << AP_M68882_FPCC_I, "+infinity"},
      {AP_M68882_RESULT_INFINITY, true,
       (1u << AP_M68882_FPCC_N) | (1u << AP_M68882_FPCC_I), "-infinity"},
      {AP_M68882_RESULT_NAN, false, 1u << AP_M68882_FPCC_NAN, "+NAN"},
      {AP_M68882_RESULT_NAN, true,
       (1u << AP_M68882_FPCC_N) | (1u << AP_M68882_FPCC_NAN), "-NAN"},
  };

  for (unsigned i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    ap_m68882_set_condition(&regs, ROWS[i].kind, ROWS[i].negative);
    /* The four condition bits, and nothing else in the register. */
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(ROWS[i].expected, regs.fpsr, ROWS[i].what);
  }
}

/* A zero still has a sign on this part -- `-0` sets N as well as Z -- which is
 * why the sign is a separate argument rather than being derived from the kind.
 * A model deriving it would make -0 indistinguishable from +0 in the condition
 * codes, and Table 2-1 gives them different rows. */
static void test_a_negative_zero_sets_both_n_and_z(void) {
  ap_m68882_regs_t positive;
  ap_m68882_regs_t negative;
  ap_m68882_regs_reset(&positive);
  ap_m68882_regs_reset(&negative);
  ap_m68882_set_condition(&positive, AP_M68882_RESULT_ZERO, false);
  ap_m68882_set_condition(&negative, AP_M68882_RESULT_ZERO, true);

  TEST_ASSERT_NOT_EQUAL_UINT32(positive.fpsr, negative.fpsr);
  TEST_ASSERT_TRUE(ap_m68882_condition_equal(&positive));
  TEST_ASSERT_TRUE(ap_m68882_condition_equal(&negative));
}

/* The four IEEE conditions, derived from the bits rather than stored:
 * `EQ = Z`, `GT = !(NAN|Z|N)`, `LT = N & !(NAN|Z)`, `UN = NAN`.
 *
 * Swept over **every** combination of the four bits, including the eight the
 * part never generates -- an `FMOVE` to the status register can write any of
 * them, and "loading the FPCC byte with one of the other condition code bit
 * combinations and executing a conditional instruction may produce an
 * unexpected branch condition". Unexpected is not undefined: the conditions
 * still follow the equations, and that is what this pins. */
static void test_the_ieee_conditions_follow_the_equations_everywhere(void) {
  for (unsigned bits = 0; bits < 16u; bits++) {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    const bool nan = (bits & 1u) != 0u;
    const bool i = (bits & 2u) != 0u;
    const bool z = (bits & 4u) != 0u;
    const bool n = (bits & 8u) != 0u;
    regs.fpsr = (uint32_t)((nan ? 1u << AP_M68882_FPCC_NAN : 0u) |
                           (i ? 1u << AP_M68882_FPCC_I : 0u) |
                           (z ? 1u << AP_M68882_FPCC_Z : 0u) |
                           (n ? 1u << AP_M68882_FPCC_N : 0u));

    TEST_ASSERT_EQUAL_INT(z, ap_m68882_condition_equal(&regs));
    TEST_ASSERT_EQUAL_INT(!(nan || z || n), ap_m68882_condition_greater(&regs));
    TEST_ASSERT_EQUAL_INT(n && !(nan || z), ap_m68882_condition_less(&regs));
    TEST_ASSERT_EQUAL_INT(nan, ap_m68882_condition_unordered(&regs));

    /* And the four are never all false and never contradictory: greater and
     * less cannot both hold. */
    TEST_ASSERT_FALSE(ap_m68882_condition_greater(&regs) &&
                      ap_m68882_condition_less(&regs));
  }
}

/* An exception bit is recorded whether or not its trap is enabled: "the
 * corresponding bit in the EXC byte is set, even if the trap for that exception
 * class is disabled". The enable byte decides whether a *trap* is taken, never
 * whether the bit is recorded -- and a model that gated the record on the
 * enable would lose the accrued history for every disabled class, which is
 * exactly the case the accrued byte exists for. */
static void test_an_exception_is_recorded_even_when_its_trap_is_disabled(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  TEST_ASSERT_EQUAL_HEX32(0u, regs.fpcr); /* every trap disabled */

  ap_m68882_raise_exception(&regs, AP_M68882_EXC_DZ);
  TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_EXC_DZ) & 1u) != 0u);
  TEST_ASSERT_FALSE(ap_m68882_exception_enabled(&regs, AP_M68882_EXC_DZ));

  /* Enabling it afterwards makes the same bit a trap, which is the single AND
   * the shared bit positions exist for. */
  regs.fpcr |= UINT32_C(1) << AP_M68882_EXC_DZ;
  TEST_ASSERT_TRUE(ap_m68882_exception_enabled(&regs, AP_M68882_EXC_DZ));
}

/* The five accrued equations, each checked on its own. Three are plain ORs and
 * two are not, and the two are where a model goes wrong silently. */
static void test_the_accrued_equations(void) {
  /* AEXC(IOP) is set by any of three classes. */
  const unsigned invalid[] = {AP_M68882_EXC_BSUN, AP_M68882_EXC_SNAN,
                              AP_M68882_EXC_OPERR};
  for (unsigned i = 0; i < 3u; i++) {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    ap_m68882_raise_exception(&regs, invalid[i]);
    ap_m68882_accrue(&regs);
    TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_AEXC_IOP) & 1u) != 0u);
  }

  /* AEXC(DZ) and AEXC(OVFL) are one-to-one. */
  {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    ap_m68882_raise_exception(&regs, AP_M68882_EXC_DZ);
    ap_m68882_accrue(&regs);
    TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_AEXC_DZ) & 1u) != 0u);
    TEST_ASSERT_FALSE(((regs.fpsr >> AP_M68882_AEXC_INEX) & 1u) != 0u);
  }
}

/* **`AEXC(UNFL)` is an AND**, where every other equation is an OR: "AEXC(UNFL)
 * = AEXC(UNFL) v EXC(UNFL ^ INEX2)". An underflow that was *exact* does not
 * accrue. Written as the obvious OR it produces a sticky bit set far too often,
 * and nothing ever faults. */
static void test_an_exact_underflow_does_not_accrue(void) {
  ap_m68882_regs_t exact;
  ap_m68882_regs_reset(&exact);
  ap_m68882_raise_exception(&exact, AP_M68882_EXC_UNFL);
  ap_m68882_accrue(&exact);
  TEST_ASSERT_FALSE(((exact.fpsr >> AP_M68882_AEXC_UNFL) & 1u) != 0u);

  ap_m68882_regs_t inexact;
  ap_m68882_regs_reset(&inexact);
  ap_m68882_raise_exception(&inexact, AP_M68882_EXC_UNFL);
  ap_m68882_raise_exception(&inexact, AP_M68882_EXC_INEX2);
  ap_m68882_accrue(&inexact);
  TEST_ASSERT_TRUE(((inexact.fpsr >> AP_M68882_AEXC_UNFL) & 1u) != 0u);
}

/* **An overflow accrues inexactness too**: "AEXC(INEX) = AEXC(INEX) v
 * EXC(INEX1 v INEX2 v OVFL)". An overflowed result is by definition not the
 * exact one, and a model listing only the two INEX classes would drop it. */
static void test_an_overflow_accrues_inexact_as_well(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  ap_m68882_raise_exception(&regs, AP_M68882_EXC_OVFL);
  ap_m68882_accrue(&regs);

  TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_AEXC_OVFL) & 1u) != 0u);
  TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_AEXC_INEX) & 1u) != 0u);
}

/* The accrued byte is sticky where the exception byte is not: it "contains the
 * history of all floating-point exceptions that have occurred since the user
 * last cleared" it, and is cleared by the part "only by a reset or a restore
 * operation of the null state". So clearing the exception byte between
 * operations -- which the part does at the start of most -- must leave the
 * accrued bits standing. */
static void test_the_accrued_byte_survives_the_exception_byte_clearing(void) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  ap_m68882_raise_exception(&regs, AP_M68882_EXC_DZ);
  ap_m68882_accrue(&regs);

  /* The start of the next operation clears the exception byte. */
  regs.fpsr &= ~(UINT32_C(0xFF) << 8);
  TEST_ASSERT_TRUE(((regs.fpsr >> AP_M68882_AEXC_DZ) & 1u) != 0u);

  /* Only a reset clears it. */
  ap_m68882_regs_reset(&regs);
  TEST_ASSERT_EQUAL_HEX32(0u, regs.fpsr);
}

/* The quotient byte's sign is bit 23 and its magnitude is the seven bits below
 * it -- "the seven least-significant bits of the quotient (unsigned) and the
 * sign of the entire quotient". Eight bits would swallow the sign into the
 * magnitude and give every large quotient a negative one. */
static void test_the_quotient_byte_is_seven_bits_and_a_sign(void) {
  TEST_ASSERT_EQUAL_UINT(0x7Fu, (unsigned)AP_M68882_QUOTIENT_MASK);
  TEST_ASSERT_EQUAL_UINT(16u, (unsigned)AP_M68882_QUOTIENT_SHIFT);
  /* The sign sits immediately above the seven, not inside them. */
  TEST_ASSERT_EQUAL_UINT(
      (unsigned)AP_M68882_QUOTIENT_SIGN,
      (unsigned)AP_M68882_QUOTIENT_SHIFT + 7u);
}

/* The enable and status bytes occupy the same positions, which is what makes
 * the trap test a single AND. Asserted directly, because it is a property of
 * the *layout* rather than of any one field, and a transcription that moved one
 * byte would still pass every per-bit test. */
static void test_the_enable_and_status_bytes_share_their_positions(void) {
  const unsigned bits[] = {AP_M68882_EXC_BSUN,  AP_M68882_EXC_SNAN,
                           AP_M68882_EXC_OPERR, AP_M68882_EXC_OVFL,
                           AP_M68882_EXC_UNFL,  AP_M68882_EXC_DZ,
                           AP_M68882_EXC_INEX2, AP_M68882_EXC_INEX1};
  for (unsigned i = 0; i < 8u; i++) {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    regs.fpcr = UINT32_C(1) << bits[i];
    ap_m68882_raise_exception(&regs, bits[i]);
    TEST_ASSERT_TRUE(ap_m68882_exception_enabled(&regs, bits[i]));

    /* And no *other* class is enabled by it, which is what a one-bit offset
     * between the two bytes would break. */
    for (unsigned k = 0; k < 8u; k++) {
      if (k != i) {
        TEST_ASSERT_FALSE(ap_m68882_exception_enabled(&regs, bits[k]));
      }
    }
  }

  /* "The bits of the ENABLE byte are organized in decreasing priority, left to
   * right, i.e., BSUN is the highest priority, and INEX1 is the lowest." */
  for (unsigned i = 1; i < 8u; i++) {
    TEST_ASSERT_TRUE(bits[i - 1u] > bits[i]);
  }
}

static void test_the_inexact_trap_has_its_own_equation(void) {
  /* §6.1.10 gives it as an equation rather than a bit test:
   *
   *     Inexact Trap =
   *       [[EXC(OVFL) v EXC(INEX2)] ^ ENABLE(INEX2)] v [EXC(INEX1) ^ ENABLE(INEX1)]
   *
   * The `OVFL` term is what a plain bit-against-bit test loses: enabling the
   * inexact trap makes an **overflow** trap too, whether or not `INEX2` is set
   * alongside it.
   *
   * The arithmetic in this core happens to set `INEX2` on every overflow, which
   * IEEE 754 requires -- so the simple test would give the right answer today.
   * That is exactly why this is worth transcribing: it is two modules agreeing
   * by luck, and the equation holds whether or not they continue to. Each row
   * below is one term of it, and the fourth sets `OVFL` *without* `INEX2` to
   * exercise the term that would otherwise never be reached. */
  const struct {
    unsigned exc_bits;
    unsigned enable_bits;
    bool trap;
    const char *why;
  } rows[] = {
      {0u, 0u, false, "nothing set"},
      {1u << AP_M68882_EXC_INEX2, 0u, false, "inexact but not enabled"},
      {1u << AP_M68882_EXC_INEX2, 1u << AP_M68882_EXC_INEX2, true,
       "the ordinary inexact trap"},
      {1u << AP_M68882_EXC_OVFL, 1u << AP_M68882_EXC_INEX2, true,
       "an overflow traps through ENABLE(INEX2)"},
      {1u << AP_M68882_EXC_OVFL, 1u << AP_M68882_EXC_OVFL, false,
       "ENABLE(OVFL) is a different trap, not this one"},
      {1u << AP_M68882_EXC_INEX1, 1u << AP_M68882_EXC_INEX2, false,
       "INEX1 needs its own enable"},
      {1u << AP_M68882_EXC_INEX1, 1u << AP_M68882_EXC_INEX1, true,
       "the decimal-input inexact trap"},
  };
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    ap_m68882_regs_t regs;
    ap_m68882_regs_reset(&regs);
    regs.fpsr |= rows[i].exc_bits;
    regs.fpcr |= rows[i].enable_bits;
    TEST_ASSERT_EQUAL_MESSAGE(rows[i].trap, ap_m68882_inexact_trap(&regs),
                              rows[i].why);
  }
}

static void test_both_inexact_bits_share_one_vector(void) {
  /* §6.1.7: "note that only one inexact exception vector number is generated by
   * the FPCP. If either of the two inexact exceptions is enabled, the MPU
   * fetches the inexact exception vector." So the equation returns one answer
   * for two bits, and a model with a trap per bit would take two exceptions for
   * an `FDIV.P` that was inexact in both its decimal input and its divide --
   * which §6.1.7 gives as the very example for why two bits exist. */
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  regs.fpsr |= (1u << AP_M68882_EXC_INEX1) | (1u << AP_M68882_EXC_INEX2);
  regs.fpcr |= (1u << AP_M68882_EXC_INEX1) | (1u << AP_M68882_EXC_INEX2);
  TEST_ASSERT_TRUE(ap_m68882_inexact_trap(&regs));

  /* And either enable alone is enough, which is what "if either ... is enabled"
   * means. */
  ap_m68882_regs_t only_one;
  ap_m68882_regs_reset(&only_one);
  only_one.fpsr |= (1u << AP_M68882_EXC_INEX1) | (1u << AP_M68882_EXC_INEX2);
  only_one.fpcr |= 1u << AP_M68882_EXC_INEX1;
  TEST_ASSERT_TRUE(ap_m68882_inexact_trap(&only_one));
}

/* The condition-code state a comparison leaves: `N`, `Z` and `NAN`. */
static ap_m68882_regs_t with_condition(bool n, bool z, bool nan) {
  ap_m68882_regs_t regs;
  ap_m68882_regs_reset(&regs);
  if (n) regs.fpsr |= 1u << AP_M68882_FPCC_N;
  if (z) regs.fpsr |= 1u << AP_M68882_FPCC_Z;
  if (nan) regs.fpsr |= 1u << AP_M68882_FPCC_NAN;
  return regs;
}

static void test_the_thirty_two_predicates_over_every_condition(void) {
  /* §4.4's three tables, checked against an independent statement of what each
   * predicate *means* rather than against a second copy of its equation.
   *
   * The four states a comparison can leave are: greater (nothing set), equal
   * (`Z`), less (`N`), and unordered (`NAN`). Naming the answer in those terms
   * and letting the table decide is what makes this a test rather than a
   * transcription of the same booleans twice -- a mistyped equation would agree
   * with itself and disagree with the column below. */
  enum { GREATER, EQUAL, LESS, UNORDERED, STATES };
  const struct { bool n, z, nan; } state[STATES] = {
      {false, false, false}, {false, true, false},
      {true, false, false},  {false, false, true}};

  const struct {
    unsigned predicate;
    const char *name;
    bool expect[STATES]; /* greater, equal, less, unordered */
  } rows[] = {
      /* The ordered (IEEE-aware) half: an unordered operand answers plainly. */
      {0x00u, "F",    {false, false, false, false}},
      {0x01u, "EQ",   {false, true,  false, false}},
      {0x02u, "OGT",  {true,  false, false, false}},
      {0x03u, "OGE",  {true,  true,  false, false}},
      {0x04u, "OLT",  {false, false, true,  false}},
      {0x05u, "OLE",  {false, true,  true,  false}},
      {0x06u, "OGL",  {true,  false, true,  false}},
      {0x07u, "OR",   {true,  true,  true,  false}},
      {0x08u, "UN",   {false, false, false, true}},
      {0x09u, "UEQ",  {false, true,  false, true}},
      {0x0Au, "UGT",  {true,  false, false, true}},
      {0x0Bu, "UGE",  {true,  true,  false, true}},
      {0x0Cu, "ULT",  {false, false, true,  true}},
      {0x0Du, "ULE",  {false, true,  true,  true}},
      {0x0Eu, "NE",   {true,  false, true,  true}},
      {0x0Fu, "T",    {true,  true,  true,  true}},
  };

  for (unsigned r = 0; r < 16u; r++) {
    for (unsigned st = 0; st < STATES; st++) {
      const ap_m68882_regs_t regs =
          with_condition(state[st].n, state[st].z, state[st].nan);
      const ap_m68882_condition_t low =
          ap_m68882_evaluate_condition(&regs, rows[r].predicate);
      TEST_ASSERT_EQUAL_MESSAGE(rows[r].expect[st], low.taken, rows[r].name);

      /* The high half is the same sixteen equations: every predicate in
       * `$10-$1F` must answer identically to its partner in `$00-$0F`. If that
       * ever stops holding, one of the thirty-two rows has been mistyped. */
      const ap_m68882_condition_t high =
          ap_m68882_evaluate_condition(&regs, rows[r].predicate | 0x10u);
      TEST_ASSERT_EQUAL_MESSAGE(
          low.taken, high.taken,
          "a high predicate disagreed with its low partner");
    }
  }
}

static void test_bsun_is_bit_four_against_the_nan_condition(void) {
  /* §4.4.2: the IEEE-aware tests "do not set the BSUN bit in the status
   * register exception byte under any circumstances". §4.4.1: the non-aware
   * ones do, "if the NAN condition code bit is set when a conditional
   * instruction is executed". §4.4.3 puts `F` and `T` in the first group and
   * `SF`, `ST`, `SEQ` and `SNE` in the second.
   *
   * §6.1.1 phrases the rule as "except EQ and NE", which reads like a special
   * case and is not one: both live at `$01` and `$0E`, in the low group, so the
   * encoding already excludes them. The whole rule is one bit against one
   * condition code. */
  for (unsigned p = 0; p <= 0x1Fu; p++) {
    const ap_m68882_regs_t unordered = with_condition(false, false, true);
    const ap_m68882_condition_t got =
        ap_m68882_evaluate_condition(&unordered, p);
    TEST_ASSERT_EQUAL_MESSAGE((p & 0x10u) != 0u, got.bsun,
                              "BSUN should follow bit 4 exactly");

    /* And never without an unordered operand, whatever the predicate. */
    for (unsigned st = 0; st < 3u; st++) {
      const ap_m68882_regs_t ordered =
          with_condition(st == 2u, st == 1u, false);
      TEST_ASSERT_FALSE_MESSAGE(
          ap_m68882_evaluate_condition(&ordered, p).bsun,
          "an ordered comparison can never raise BSUN");
    }
  }

  /* The four §4.4.3 names the manual calls out, spelled out so the claim is
   * checkable against the sentence rather than only against the loop. */
  const ap_m68882_regs_t nan = with_condition(false, false, true);
  TEST_ASSERT_FALSE(ap_m68882_evaluate_condition(&nan, 0x00u).bsun); /* F  */
  TEST_ASSERT_FALSE(ap_m68882_evaluate_condition(&nan, 0x0Fu).bsun); /* T  */
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&nan, 0x10u).bsun);  /* SF */
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&nan, 0x1Fu).bsun);  /* ST */
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&nan, 0x11u).bsun);  /* SEQ*/
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&nan, 0x1Eu).bsun);  /* SNE*/
  /* `EQ` and `NE`, §6.1.1's named exception, are in the low group. */
  TEST_ASSERT_FALSE(ap_m68882_evaluate_condition(&nan, 0x01u).bsun);
  TEST_ASSERT_FALSE(ap_m68882_evaluate_condition(&nan, 0x0Eu).bsun);
}

static void test_the_branches_lack_trichotomy(void) {
  /* The manual's own warning, and the reason the aware set exists: "compiler
   * programmers should be particularly careful of the lack of trichotomy in the
   * floating-point branches since it is common for compilers to invert the
   * sense of conditions."
   *
   * With an unordered operand, `FBGT` and `FBLE` are **both false** -- so a
   * compiler that emitted `NOT greater than` where it meant `less or equal`
   * would take the wrong branch. `FBNGT` is true where `FBGT` is false, which
   * is what "not greater than" has to mean once unordered exists. */
  const ap_m68882_regs_t unordered = with_condition(false, false, true);
  TEST_ASSERT_FALSE_MESSAGE(
      ap_m68882_evaluate_condition(&unordered, 0x12u).taken, "GT");
  TEST_ASSERT_FALSE_MESSAGE(
      ap_m68882_evaluate_condition(&unordered, 0x15u).taken, "LE");
  TEST_ASSERT_TRUE_MESSAGE(
      ap_m68882_evaluate_condition(&unordered, 0x1Du).taken, "NGT");
  /* Both of the first two raise `BSUN`, which is exactly how the non-aware
   * program finds out that something unexpected happened. */
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&unordered, 0x12u).bsun);
  TEST_ASSERT_TRUE(ap_m68882_evaluate_condition(&unordered, 0x15u).bsun);
}

static void test_an_undefined_predicate_is_not_indexed(void) {
  /* Table 4-8 defines `$00-$1F` and nothing above it. A six-bit field can hold
   * more, and reading a table with an encoding the manual never defines is how
   * a decoder invents behaviour. */
  const ap_m68882_regs_t regs = with_condition(false, false, true);
  for (unsigned p = 0x20u; p <= 0x3Fu; p++) {
    const ap_m68882_condition_t got = ap_m68882_evaluate_condition(&regs, p);
    TEST_ASSERT_FALSE(got.taken);
    TEST_ASSERT_FALSE(got.bsun);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_leaves_nans_rather_than_zeros);
  RUN_TEST(test_the_mode_byte_fields_are_at_their_own_bits);
  RUN_TEST(test_the_reserved_precision_is_reported_as_reserved);
  RUN_TEST(test_table_2_1_condition_codes_in_full);
  RUN_TEST(test_a_negative_zero_sets_both_n_and_z);
  RUN_TEST(test_the_ieee_conditions_follow_the_equations_everywhere);
  RUN_TEST(test_an_exception_is_recorded_even_when_its_trap_is_disabled);
  RUN_TEST(test_the_accrued_equations);
  RUN_TEST(test_an_exact_underflow_does_not_accrue);
  RUN_TEST(test_an_overflow_accrues_inexact_as_well);
  RUN_TEST(test_the_accrued_byte_survives_the_exception_byte_clearing);
  RUN_TEST(test_the_quotient_byte_is_seven_bits_and_a_sign);
  RUN_TEST(test_the_enable_and_status_bytes_share_their_positions);
  RUN_TEST(test_the_thirty_two_predicates_over_every_condition);
  RUN_TEST(test_bsun_is_bit_four_against_the_nan_condition);
  RUN_TEST(test_the_branches_lack_trichotomy);
  RUN_TEST(test_an_undefined_predicate_is_not_indexed);
  RUN_TEST(test_the_inexact_trap_has_its_own_equation);
  RUN_TEST(test_both_inexact_bits_share_one_vector);
  return UNITY_END();
}
