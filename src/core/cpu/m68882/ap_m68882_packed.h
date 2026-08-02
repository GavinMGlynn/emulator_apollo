/* MC68882 packed decimal real, §3.3 and §3.6.
 *
 * The one operand format that is not a binary one, and the only one whose
 * conversion is arithmetic rather than field extraction.
 *
 * ## Ninety-six bits, and the type is not in the exponent alone
 *
 * Figure 3-11: `SM` at bit 95, `SE` at 94, two `y` bits at 93-92 "used only for
 * +/-infinity or NAN(s); zero otherwise", three exponent digits at 91-80, a
 * don't-care field at 79-68, the integer digit `MANT16` at 67-64 with the
 * decimal point implicit after it, and sixteen fraction digits below.
 *
 * Table 3-4 decides the data type, and **not by the exponent alone** as the
 * binary formats do: an infinity or NAN has `SE` *and* both `y` bits set *and*
 * an exponent of `$FFF`, and only then is infinity told from NAN by the fraction
 * being zero.
 *
 * ## A NAN is copied, not converted
 *
 * Note 1: "the fraction part of the NAN is moved bit-for-bit into the extended
 * precision mantissa of a floating-point register. The exponent of the register
 * is set to signify a NAN, but **no decimal-to-binary conversion or any other
 * conversion is performed**." The signalling bit then falls exactly where the
 * extended format already puts it -- `MANT15`'s most significant bit becomes the
 * extended integer bit and is a don't care "as in extended NANs", and the bit
 * below it is the SNAN bit, which is extended bit 62.
 *
 * ## Non-decimal digits are defined behaviour, not an error
 *
 * Note 2: `$A`-`$F` in the exponent of a *zero* makes it a true zero, but
 * otherwise "The FPCP does not detect non-decimal digits ... These non-decimal
 * digits are converted to binary in the same manner as decimal digits; however,
 * the result is probably useless, although it is **repeatable**." Repeatable is
 * the operative word: a converter that rejected them would refuse strings the
 * part accepts, and one that clamped them would not be repeatable.
 *
 * ## The rounding rule is a third variant
 *
 * §6.1.8: "the result of the decimal-to-binary conversion is rounded to extended
 * precision (**regardless of FPSR mode byte rounding precision**)". So the three
 * rules in this part are all different -- a store to memory rounds to the
 * destination format and ignores `PREC`, `FMOVECR` rounds to `PREC`, and this
 * rounds to extended whatever `PREC` says.
 *
 * And it is a *correct rounding* requirement rather than a bound, which is why
 * this module carries multi-word integer arithmetic: `INEX1` is "the condition
 * that exists when a packed decimal operand cannot be converted exactly to
 * extended precision in the current rounding mode", so the exact value of
 * `M x 10^E` has to be known before it can be said to be inexact. `5^999` alone
 * is some 2322 bits.
 *
 * Note 3 bounds the problem in one direction: "Since in-range numbers cannot
 * overflow or underflow when converted to extended precision, normalized
 * extended precision numbers are always produced by conversion from the decimal
 * data format." The widest string is about `9.9e999` against extended's
 * `1.19e4932`, so there is no overflow case to model on the way in.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_PACKED_H
#define APOLLO_CPU_M68882_AP_M68882_PACKED_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_regs.h"

/* Decode a packed decimal string into extended precision, correctly rounded.
 *
 * `bytes` holds the twelve bytes in memory order, most significant first.
 * `*exceptions` receives `INEX1` when the value is not exactly representable;
 * nothing else can be raised, since Note 3 rules out overflow and underflow. */
void ap_m68882_packed_decode(const uint8_t *bytes, ap_m68882_rounding_t mode,
                             ap_m68882_extended_t *out, uint32_t *exceptions);

/* Encode an extended value as a packed decimal string.
 *
 * `k_factor` is the seven-bit signed field: "-64 to 0 -- Indicates the number of
 * significant digit to the right of the decimal point (Fortran 'F' format).
 * +1 to +17 -- Indicates the number of significant digits in the mantissa
 * (Fortran 'E' format). +18 to +63 -- Sets the OPERR bit in the FPSR exception
 * byte, treated as +17" -- both halves of that last row, so an out-of-range
 * k-factor raises *and* still produces the result the part produces.
 *
 * `*exceptions` receives `OPERR` for that case and for a decimal exponent past
 * 999, and `INEX2` when the string cannot represent the value exactly. `INEX2`
 * rather than `INEX1`: §3.6 sends binary-to-decimal inaccuracy to §6.1.7, and
 * the two bits are separate precisely so a program can tell the directions
 * apart.
 *
 * **Accuracy, and a documented absence.** §3.6 is explicit that the part itself
 * is not correctly rounded here: "the error bounds specified by the IEEE
 * standard apply only to conversions of values in the range of the double
 * precision format. The error bound for conversions by the FPCP of extended
 * precision values which cannot be represented in double precision is
 * significantly larger. Software must be provided to convert such extended
 * precision values to decimal." No bound is published for that case, so there
 * is no figure to model. This rounds correctly at every magnitude, which is
 * inside the IEEE bound where one applies and better than the part where none
 * does -- the same position the transcendentals take, and recorded in
 * `PROJECT_STATUS.md` rather than left implicit. */
void ap_m68882_packed_encode(const ap_m68882_extended_t *value, int k_factor,
                             ap_m68882_rounding_t mode, uint8_t *bytes,
                             uint32_t *exceptions);

#endif /* APOLLO_CPU_M68882_AP_M68882_PACKED_H */
