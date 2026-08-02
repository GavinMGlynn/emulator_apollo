/* MC68882 binary real data formats: single, double and extended.
 *
 * `MC68881/MC68882 User's Manual` §3.2, Figures 3-2 through 3-4.
 *
 * ## Everything becomes extended
 *
 * "Since all FPCP internal operations are performed in extended precision,
 * single and double precision operands are converted to extended precision
 * values before the specified operation is performed. Thus, mixed mode
 * arithmetic is implicitly supported." So the conversions are not a convenience
 * for callers -- they are on the path of every single- or double-precision
 * operand the part ever sees, and an error in them is an error in every
 * operation at that precision.
 *
 * ## The explicit integer bit, and the exception it creates
 *
 * Single and double store only a fraction and imply the leading one; extended
 * stores the whole mantissa including that bit. The manual states the
 * consequence as a NOTE, and it is the trap in this module:
 *
 *   "There is a subtle difference between the definition of an extended
 *   precision number with an exponent equal to zero and a single or double
 *   precision number with an exponent equal to zero. If the exponent of a single
 *   or double precision number is zero, the number is defined to be
 *   denormalized ... However, an extended precision number with an exponent of
 *   zero may have an explicit integer bit equal to one, which results in a
 *   **normalized** number (even though the exponent is equal to the minimum
 *   value)."
 *
 * So "exponent zero means denormalized" is true of two formats out of three,
 * and a classifier written once and applied to all three misreads exactly the
 * numbers the extended format exists to represent. The manual itself flags that
 * it will go on using the simple rule "for simplicity" -- which is why the rule
 * has to be read from the NOTE rather than from the surrounding text.
 *
 * ## 96 bits of which 80 are used
 *
 * The extended format occupies three long words: sign and exponent in the
 * first, **sixteen unused bits** in the rest of it, then the 64-bit mantissa.
 * A model packing it as 80 contiguous bits reads every mantissa 16 bits out of
 * place once it touches memory.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_FORMAT_H
#define APOLLO_CPU_M68882_AP_M68882_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_regs.h"

/* "The bias values for single, double, and extended precision are 127, 1023,
 * and 16383, respectively." */
enum {
  AP_M68882_BIAS_SINGLE = 127,
  AP_M68882_BIAS_DOUBLE = 1023,
  AP_M68882_BIAS_EXTENDED = 16383,
};

/* "Each of the three floating-point data formats can represent five unique
 * floating-point data types." */
typedef enum {
  AP_M68882_TYPE_NORMALIZED,
  AP_M68882_TYPE_DENORMALIZED,
  AP_M68882_TYPE_ZERO,
  AP_M68882_TYPE_INFINITY,
  AP_M68882_TYPE_NAN,
} ap_m68882_type_t;

/* Classify an extended-precision value.
 *
 * **Not** by the single/double rule. An exponent of zero with the integer bit
 * set is a *normalized* number here, which is the NOTE quoted in the header and
 * the reason this takes an extended value rather than a bare exponent. */
[[nodiscard]] ap_m68882_type_t
ap_m68882_classify(const ap_m68882_extended_t *value);

/* Whether a NAN is signalling. The IEEE distinction is the most significant
 * fraction bit: clear means signalling. On this format the mantissa's bit 63 is
 * the integer bit, so the fraction begins at bit 62 -- taking bit 63 instead
 * would call every NAN signalling, since a NAN's integer bit is set. */
[[nodiscard]] bool ap_m68882_is_signalling_nan(const ap_m68882_extended_t *value);

/* Single precision, `[68881]` Figure 3-2: sign at bit 31, an 8-bit exponent at
 * 30-23, and a 23-bit fraction with the leading one implied. */
[[nodiscard]] ap_m68882_extended_t ap_m68882_from_single(uint32_t bits);
[[nodiscard]] uint32_t ap_m68882_to_single(const ap_m68882_extended_t *value);

/* Double precision: sign at bit 63, an 11-bit exponent at 62-52, and a 52-bit
 * fraction with the leading one implied. */
[[nodiscard]] ap_m68882_extended_t ap_m68882_from_double(uint64_t bits);
[[nodiscard]] uint64_t ap_m68882_to_double(const ap_m68882_extended_t *value);

/* Extended precision in memory: three long words, "96 bits, 80 of which are
 * used". The first holds the sign and the 15-bit exponent; the **sixteen bits
 * below them are unused**; the mantissa fills the other two long words.
 *
 * `high` is the first long word and `mantissa` the other two, so a caller
 * reading memory passes what it read rather than having to know where the gap
 * is. */
[[nodiscard]] ap_m68882_extended_t ap_m68882_from_extended(uint32_t high,
                                                           uint64_t mantissa);
void ap_m68882_to_extended(const ap_m68882_extended_t *value, uint32_t *high,
                           uint64_t *mantissa);

/* Signed two's complement integers, §3.1 Figure 3-1: "identical to those
 * supported by the M68000 Family architecture". Byte and word operands are
 * sign-extended by the caller, so one entry point covers all three widths.
 *
 * Exact for every input -- a 32-bit integer fits the 64-bit mantissa with 32
 * bits to spare -- so this raises no exception and needs no rounding mode. */
[[nodiscard]] ap_m68882_extended_t ap_m68882_from_integer(int32_t value);

/* The source specifier's data formats, §4.8.4: "If R/M = 1, it specifies the
 * source operand data format." The encoding is not ordered by width and not
 * ordered by kind -- the three integer formats are at 000, 100 and 110 with the
 * reals interleaved -- so it is transcribed rather than computed.
 *
 * $7 is not a source format. In the store direction it is packed decimal with a
 * dynamic k-factor, and as a *source* specifier the value means FMOVECR, which
 * fetches no operand at all. */
typedef enum {
  AP_M68882_FORMAT_LONG = 0,
  AP_M68882_FORMAT_SINGLE = 1,
  AP_M68882_FORMAT_EXTENDED = 2,
  AP_M68882_FORMAT_PACKED = 3,
  AP_M68882_FORMAT_WORD = 4,
  AP_M68882_FORMAT_DOUBLE = 5,
  AP_M68882_FORMAT_BYTE = 6,
  AP_M68882_FORMAT_PACKED_DYNAMIC = 7,
} ap_m68882_format_t;

/* How many bytes the format occupies in memory. This is what decides the bus
 * traffic an operand transfer generates, so it is a hardware figure and not a
 * buffer size: extended is **twelve**, the 80 used bits plus the 16 unused
 * ones, and packed decimal is twelve as well (§3.3 Figure 3-9 runs bit 91 down
 * to bit 0). */
[[nodiscard]] unsigned ap_m68882_format_size(ap_m68882_format_t format);

/* Decode an operand as it lies in memory into the internal extended value.
 *
 * `mode` and `exceptions` matter for exactly one format: packed decimal, whose
 * conversion is arithmetic and can be inexact. Every binary format is exact on
 * the way in and ignores both.
 *
 * `bytes` holds `ap_m68882_format_size(format)` bytes in memory order, which §3
 * states for every format: "the most-significant byte is located at the lowest
 * address ... The least-significant byte is located at the highest address".
 * Taking them as bytes rather than as assembled words keeps the extended
 * format's sixteen unused bits a fact of this module, where the rest of the
 * format already lives, instead of one the main processor has to know.
 *
 * Returns false only for a format that is not a source format at all. */
[[nodiscard]] bool ap_m68882_operand_decode(ap_m68882_format_t format,
                                            const uint8_t *bytes,
                                            ap_m68882_rounding_t mode,
                                            ap_m68882_extended_t *out,
                                            uint32_t *exceptions);

#endif /* APOLLO_CPU_M68882_AP_M68882_FORMAT_H */
