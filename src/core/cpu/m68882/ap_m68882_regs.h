/* MC68882 programming model: the three control registers and the eight
 * floating-point data registers.
 *
 * `MC68881/MC68882 Floating-Point Coprocessor User's Manual 1ed 1987` §2,
 * Figures 2-2 through 2-7 and Table 2-1.
 *
 * ## Why this is the first piece
 *
 * The same reason `ap_m68030_regs` was the 68030's: it is the part that is pure
 * transcription. Bit positions, the condition-code table and the accrued-
 * exception equations are all *stated*, so they can be got right before any
 * arithmetic exists to get wrong -- and every later piece is checked against
 * them rather than the other way round.
 *
 * The DN3500 has a 68882, so this is the only member of the CPU family beyond
 * the 68030 that is on the machine's critical path.
 *
 * ## The two exception bytes occupy the same positions, deliberately
 *
 * "Note that the bits in the FPSR exception byte and the FPCR enable byte
 * occupy the same positions within each byte." That is what lets an exception
 * be signalled by a single AND of the two, and it is why one set of bit
 * definitions serves both. A model giving them separate layouts would work
 * until the first enabled trap.
 *
 * ## The accrued byte is not the exception byte
 *
 * Five bits, not eight, and each is a *combination* -- `AEXC(IOP)` is set by any
 * of BSUN, SNAN or OPERR. It is also sticky where the exception byte is not:
 * "this byte is cleared by the FPCP at the start of most operations" against
 * "the AEXC byte is cleared by the FPCP only by a reset or a restore operation
 * of the null state". Treating them as one byte at two names would both lose
 * the history the IEEE standard requires and clear it every instruction.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_REGS_H
#define APOLLO_CPU_M68882_AP_M68882_REGS_H

#include <stdbool.h>
#include <stdint.h>

/* Eight floating-point data registers, each holding an extended-precision
 * value: a 15-bit exponent, a sign, and a 64-bit mantissa with an *explicit*
 * integer bit. Kept as the three fields rather than as an 80-bit blob because
 * every operation reads them separately, and because the explicit integer bit
 * is the difference between this format and the single and double ones -- a
 * model that packed them would have to unpack at every use and would hide it. */
#define AP_M68882_DATA_REGISTERS 8u

typedef struct {
  bool sign;
  uint16_t exponent; /* 15 bits, biased by $3FFF */
  uint64_t mantissa; /* the explicit integer bit is bit 63 */
} ap_m68882_extended_t;

/* FPCR exception enable byte, bits 15-8 (Figure 2-2), and the FPSR exception
 * status byte, bits 15-8 (Figure 2-6). One set of definitions for both, because
 * the manual puts them in the same positions on purpose.
 *
 * "The bits of the ENABLE byte are organized in decreasing priority, left to
 * right, i.e., BSUN is the highest priority, and INEX1 is the lowest." */
enum {
  AP_M68882_EXC_BSUN = 15,  /* branch/set on unordered */
  AP_M68882_EXC_SNAN = 14,  /* signalling not a number */
  AP_M68882_EXC_OPERR = 13, /* operand error */
  AP_M68882_EXC_OVFL = 12,  /* overflow */
  AP_M68882_EXC_UNFL = 11,  /* underflow */
  AP_M68882_EXC_DZ = 10,    /* divide by zero */
  AP_M68882_EXC_INEX2 = 9,  /* inexact operation */
  AP_M68882_EXC_INEX1 = 8,  /* inexact decimal input */
};

/* FPCR mode control byte, bits 7-0 (Figure 2-3). "A zero in this byte selects
 * the IEEE defaults", which is round-to-nearest at extended precision. */
typedef enum {
  AP_M68882_ROUND_NEAREST = 0,
  AP_M68882_ROUND_ZERO = 1,
  AP_M68882_ROUND_MINUS_INFINITY = 2,
  AP_M68882_ROUND_PLUS_INFINITY = 3,
} ap_m68882_rounding_t;

typedef enum {
  AP_M68882_PRECISION_EXTENDED = 0,
  AP_M68882_PRECISION_SINGLE = 1,
  AP_M68882_PRECISION_DOUBLE = 2,
  /* "11 (UNDEFINED, RESERVED)" -- carried rather than folded into one of the
   * three, so a program selecting it is visible instead of silently getting
   * double. */
  AP_M68882_PRECISION_RESERVED = 3,
} ap_m68882_precision_t;

/* FPSR condition code byte, bits 27-24 (Figure 2-4). Only four bits of the
 * byte are defined, and the other four read zero. */
enum {
  AP_M68882_FPCC_N = 27,   /* negative */
  AP_M68882_FPCC_Z = 26,   /* zero */
  AP_M68882_FPCC_I = 25,   /* infinity */
  AP_M68882_FPCC_NAN = 24, /* not a number, or unordered */
};

/* FPSR accrued exception byte, bits 7-3 (Figure 2-7). Five bits, each a
 * combination of the exception byte's eight. */
enum {
  AP_M68882_AEXC_IOP = 7,  /* invalid operation */
  AP_M68882_AEXC_OVFL = 6,
  AP_M68882_AEXC_UNFL = 5,
  AP_M68882_AEXC_DZ = 4,
  AP_M68882_AEXC_INEX = 3,
};

/* FPSR quotient byte, bits 23-16 (Figure 2-5): "the seven least-significant
 * bits of the quotient (unsigned) and the sign of the entire quotient". */
enum {
  AP_M68882_QUOTIENT_SIGN = 23,
  AP_M68882_QUOTIENT_SHIFT = 16,
  AP_M68882_QUOTIENT_MASK = 0x7Fu,
};

typedef struct {
  ap_m68882_extended_t fp[AP_M68882_DATA_REGISTERS];

  uint32_t fpcr;  /* enable byte 15-8, mode control byte 7-0 */
  uint32_t fpsr;  /* condition 27-24, quotient 23-16, exception 15-8,
                   * accrued 7-3 */
  uint32_t fpiar; /* the address of the instruction about to execute */
} ap_m68882_regs_t;

/* "The reset function or a restore operation of the null state clears the
 * FPSR", and the control register with it: a zero mode byte "selects the IEEE
 * defaults". The data registers are set to non-signalling NANs, which is what
 * the part does and is not the same as zeroing them -- a zeroed register reads
 * as +0 and would make an uninitialised program appear to work. */
void ap_m68882_regs_reset(ap_m68882_regs_t *regs);

/* The mode control byte, decoded. */
[[nodiscard]] ap_m68882_rounding_t
ap_m68882_rounding_mode(const ap_m68882_regs_t *regs);
[[nodiscard]] ap_m68882_precision_t
ap_m68882_rounding_precision(const ap_m68882_regs_t *regs);

/* Whether a given exception class is enabled to trap, which is one AND of the
 * two bytes -- the reason they share their positions. */
[[nodiscard]] bool ap_m68882_exception_enabled(const ap_m68882_regs_t *regs,
                                               unsigned exception_bit);

/* Whether an *inexact* trap is taken, which §6.1.10 gives as its own equation
 * rather than as the usual bit-against-bit test:
 *
 *     Inexact Trap =
 *       [[EXC(OVFL) v EXC(INEX2)] ^ ENABLE(INEX2)] v [EXC(INEX1) ^ ENABLE(INEX1)]
 *
 * Two things make it different from every other exception. `ENABLE(INEX2)` is
 * consulted for an **overflow**, not only for an inexact result -- so a program
 * that enables the inexact trap is trapped by an overflow whether or not
 * `INEX2` itself is set. And the two inexact bits share one vector: §6.1.7,
 * "note that only one inexact exception vector number is generated by the FPCP.
 * If either of the two inexact exceptions is enabled, the MPU fetches the
 * inexact exception vector."
 *
 * Testing `ENABLE(INEX2)` against `EXC(INEX2)` alone gives the right answer in
 * this model *only because* an overflow here also sets `INEX2` -- which IEEE 754
 * requires and §6.1.10's separate `OVFL` term suggests the part may not always
 * do. That is a coupling between two modules holding by luck, and the equation
 * is transcribed so it does not have to. */
[[nodiscard]] bool ap_m68882_inexact_trap(const ap_m68882_regs_t *regs);

/* ---------------------------------------------------------------------------
 * The conditional predicates, §4.4.
 *
 * "The FPCP supports 32 conditional tests that are separated into two groups --
 * 16 that cause an exception if an unordered condition is present when the
 * conditional test is attempted, and 16 that do not."
 *
 * The two groups are *the same sixteen equations twice*, and the encodings say
 * so: every predicate in `$10-$1F` is its partner in `$00-$0F` with bit 4 set,
 * and the pair share an equation exactly. `OGT` at `$02` and `GT` at `$12` are
 * both `~(NAN v Z v N)`; `OR` at `$07` and `GLE` at `$17` are both `~NAN`. What
 * bit 4 selects is not a different test but a different *attitude* to an
 * unordered operand: §4.4.2's IEEE-aware tests "do not set the BSUN bit ...
 * under any circumstances", while §4.4.1's non-aware ones do, so a program
 * written without the IEEE unordered concept "is interrupted if something
 * unexpected occurs".
 *
 * That reduces the whole table to sixteen equations plus one bit, which is
 * worth saying because a transcription of all thirty-two rows would be
 * thirty-two chances to mistype an equation and no way to notice.
 *
 * `EQ` and `NE` appear in both §4.4.1's and §4.4.2's tables. They are not
 * duplicated encodings -- both are the low-group `$01` and `$0E` -- and §6.1.1
 * names them as the exception when it says the non-aware set raises `BSUN`,
 * because a program can use them from either mindset without penalty. */
typedef struct {
  bool taken;
  /* Set when the predicate is one of the sixteen that raise `BSUN` *and* the
   * NAN condition code is set. Returned rather than written into the FPSR so
   * the caller can decide the trap -- §6.1.1's exception is reported by the
   * main processor, and the FPU's job ends at saying it happened. */
  bool bsun;
} ap_m68882_condition_t;

/* Evaluate one of the 32 predicates against the current condition codes.
 *
 * Predicates above `$1F` are not in Table 4-8 at all. They are reported as
 * untaken with no `BSUN`, which is the containable reading: the alternative is
 * to index a table with an encoding the manual never defines. */
[[nodiscard]] ap_m68882_condition_t
ap_m68882_evaluate_condition(const ap_m68882_regs_t *regs, unsigned predicate);

/* The result data types Table 2-1 enumerates. "Because of the mutually
 * exclusive nature of the data types described by the condition code bits, the
 * FPCP generates only eight of the 16 possible combinations", so this is what a
 * result *is* and the four bits fall out of it. Setting the bits directly would
 * make the other eight combinations reachable, and "loading the FPCC byte with
 * one of the other condition code bit combinations and executing a conditional
 * instruction may produce an unexpected branch condition". */
typedef enum {
  AP_M68882_RESULT_NORMAL,
  AP_M68882_RESULT_ZERO,
  AP_M68882_RESULT_INFINITY,
  AP_M68882_RESULT_NAN,
} ap_m68882_result_t;

void ap_m68882_set_condition(ap_m68882_regs_t *regs, ap_m68882_result_t kind,
                             bool negative);

/* The four IEEE conditions, which the manual derives from the condition code
 * bits rather than storing:
 *
 *     EQ = Z
 *     GT = !(NAN | Z | N)
 *     LT = N & !(NAN | Z)
 *     UN = NAN
 *
 * Derived and not stored, because the FPCC is what an FMOVE to the status
 * register can write directly -- so the conditions must follow whatever bits
 * are there, including combinations the part itself never generates. */
[[nodiscard]] bool ap_m68882_condition_equal(const ap_m68882_regs_t *regs);
[[nodiscard]] bool ap_m68882_condition_greater(const ap_m68882_regs_t *regs);
[[nodiscard]] bool ap_m68882_condition_less(const ap_m68882_regs_t *regs);
[[nodiscard]] bool ap_m68882_condition_unordered(const ap_m68882_regs_t *regs);

/* Set an exception in the FPSR's exception byte and fold it into the accrued
 * byte, by the manual's own five equations:
 *
 *     AEXC(IOP)  |= EXC(BSUN | SNAN | OPERR)
 *     AEXC(OVFL) |= EXC(OVFL)
 *     AEXC(UNFL) |= EXC(UNFL & INEX2)
 *     AEXC(DZ)   |= EXC(DZ)
 *     AEXC(INEX) |= EXC(INEX1 | INEX2 | OVFL)
 *
 * Two of them are worth stating aloud. `AEXC(UNFL)` is an **AND** where every
 * other is an OR -- an underflow that was exact does not accrue -- and
 * `AEXC(INEX)` is set by `OVFL`, so an overflow accrues inexactness as well as
 * overflow. Both are easy to write as the obvious OR and neither faults. */
void ap_m68882_raise_exception(ap_m68882_regs_t *regs, unsigned exception_bit);

/* Recompute the accrued byte from the exception byte, which is what happens
 * "at the end of most operations". Separate from raising, because the accrued
 * byte is folded once per instruction rather than once per exception -- and
 * `AEXC(UNFL)`'s AND cannot be evaluated until both bits are known. */
void ap_m68882_accrue(ap_m68882_regs_t *regs);

#endif /* APOLLO_CPU_M68882_AP_M68882_REGS_H */
