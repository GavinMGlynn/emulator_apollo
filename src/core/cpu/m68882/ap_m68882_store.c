/* MC68882 destination format conversion. See ap_m68882_store.h for why the
 * rounding precision does not apply and why the special cases are three tables
 * rather than one. */

#include "cpu/m68882/ap_m68882_store.h"

#include "cpu/m68882/ap_m68882_arith.h"
#include "cpu/m68882/ap_m68882_packed.h"
#include "cpu/m68882/ap_m68882_round.h"

#define INTEGER_BIT (UINT64_C(1) << 63)
/* The most significant *fraction* bit. §6.1.2 calls it "the SNAN bit", and
 * setting it is what makes a signalling NAN quiet -- clear means signalling. */
#define QUIET_BIT (UINT64_C(1) << 62)

static void put_big_endian(uint8_t *bytes, unsigned count, uint64_t value) {
  for (unsigned i = 0; i < count; i++) {
    bytes[i] = (uint8_t)(value >> (8u * (count - 1u - i)));
  }
}

/* ---------------------------------------------------------------------------
 * The binary reals
 */

/* Shape of a destination real format, so single and double are one routine.
 * Writing them twice is how one of them ends up with the other's bias. */
typedef struct {
  unsigned exponent_bits;
  unsigned fraction_bits;
  int bias;
  unsigned size;
} real_format_t;

static uint64_t assemble(const real_format_t *f, bool sign, uint32_t exponent,
                         uint64_t fraction) {
  return (sign ? (UINT64_C(1) << (f->exponent_bits + f->fraction_bits)) : 0u) |
         ((uint64_t)exponent << f->fraction_bits) |
         (fraction & ((UINT64_C(1) << f->fraction_bits) - 1u));
}

static uint64_t encode_real(const real_format_t *f,
                            const ap_m68882_extended_t *value,
                            ap_m68882_rounding_t mode, uint32_t *exceptions) {
  const uint32_t max_exponent = (1u << f->exponent_bits) - 1u;
  const bool sign = value->sign;

  switch (ap_m68882_classify(value)) {
  case AP_M68882_TYPE_ZERO:
    return assemble(f, sign, 0u, 0u);
  case AP_M68882_TYPE_INFINITY:
    return assemble(f, sign, max_exponent, 0u);
  case AP_M68882_TYPE_NAN: {
    /* §6.1.2: "If the destination data format is S, D, X, or P, then the SNAN
     * bit in the NAN is set to one and the resulting non-signaling NAN is
     * transferred to the destination. No bits other than the SNAN bit of the
     * NAN are modified, although the input NAN is **truncated** if necessary."
     *
     * Truncated, not rounded -- a NAN's significand is a payload and rounding
     * it would carry into neighbouring bits of whatever it encodes. */
    uint64_t mantissa = value->mantissa;
    if (ap_m68882_is_signalling_nan(value)) {
      *exceptions |= UINT32_C(1) << AP_M68882_EXC_SNAN;
      mantissa |= QUIET_BIT;
    }
    uint64_t fraction =
        (mantissa & ~INTEGER_BIT) >> (63u - f->fraction_bits);
    /* A NAN whose whole payload lay in the truncated bits must not arrive as an
     * infinity, which is a different data type with different condition
     * codes. */
    fraction |= UINT64_C(1) << (f->fraction_bits - 1u);
    return assemble(f, sign, max_exponent, fraction);
  }
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  /* **Normalise first.** The source may be an extended *denormal* -- integer
   * bit clear -- and the commonest way to hold one is to have loaded a single
   * or double denormal, which is this very conversion in reverse. Taking the
   * stored exponent at face value there would put the value a factor of two per
   * leading zero out, and it would land as an ordinary normalised number in the
   * destination rather than as the subnormal it is.
   *
   * The unbiased exponent is tracked as an `int` from here on, because
   * normalising a denormal walks it below what the 15-bit field can hold. */
  uint64_t mantissa = value->mantissa;
  int unbiased = (int)value->exponent - AP_M68882_BIAS_EXTENDED;
  while ((mantissa & INTEGER_BIT) == 0u) {
    mantissa <<= 1;
    unbiased--;
  }
  const int rebiased = unbiased + f->bias;

  /* Above the format's range. §6.1.4's trap-disabled result is mode-dependent
   * and *not* always an infinity: round-to-zero pulls back to the largest
   * finite magnitude, and the exception byte reads the same either way, so a
   * model returning infinity unconditionally would be wrong silently. */
  if (rebiased >= (int)max_exponent) {
    *exceptions |= (UINT32_C(1) << AP_M68882_EXC_OVFL) |
                   (UINT32_C(1) << AP_M68882_EXC_INEX2);
    const ap_m68882_precision_t precision =
        (f->fraction_bits == 23u) ? AP_M68882_PRECISION_SINGLE
                                  : AP_M68882_PRECISION_DOUBLE;
    const ap_m68882_extended_t pulled =
        ap_m68882_overflow_result(sign, mode, precision);
    if (ap_m68882_classify(&pulled) == AP_M68882_TYPE_INFINITY) {
      return assemble(f, sign, max_exponent, 0u);
    }
    return assemble(f, sign, max_exponent - 1u,
                    (pulled.mantissa & ~INTEGER_BIT) >> (63u - f->fraction_bits));
  }

  /* At or below the format's minimum normalised exponent the significand loses
   * one bit per power of two -- **gradual underflow**, and the reason this
   * rounds to a computed bit count rather than to the format's precision.
   * Rounding to full precision and then shifting would round twice. */
  unsigned keep = f->fraction_bits + 1u;
  bool subnormal = false;
  if (rebiased <= 0) {
    subnormal = true;
    const int available = (int)f->fraction_bits + rebiased;
    if (available < 1) {
      /* Not even one bit left. The answer is the smallest magnitude the format
       * has or a zero, by the same directed-rounding rule as overflow: a mode
       * that rounds away from zero cannot produce a zero from a non-zero. */
      *exceptions |= (UINT32_C(1) << AP_M68882_EXC_UNFL) |
                     (UINT32_C(1) << AP_M68882_EXC_INEX2);
      const bool away = (mode == AP_M68882_ROUND_PLUS_INFINITY && !sign) ||
                        (mode == AP_M68882_ROUND_MINUS_INFINITY && sign);
      return assemble(f, sign, 0u, away ? 1u : 0u);
    }
    keep = (unsigned)available;
  }

  /* Rounded through a value whose exponent field is the bias, so the exponent
   * cannot wrap however far the normalisation above walked: what is wanted from
   * the call is the mantissa and whether it carried, and the carry is the one
   * bit of exponent information rounding produces. */
  const ap_m68882_extended_t work = {
      .sign = sign, .exponent = AP_M68882_BIAS_EXTENDED, .mantissa = mantissa};
  const ap_m68882_round_result_t rounded =
      ap_m68882_round_to_bits(work, false, false, false, mode, keep);
  if (rounded.inexact) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_INEX2;
  }
  if (subnormal) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_UNFL;
  }

  /* Rounding can carry out of the significand and raise the exponent, which for
   * a subnormal is how it becomes the format's smallest *normal* number and for
   * a normal is how it overflows. Both are recomputed rather than assumed. */
  const bool carried = rounded.value.exponent != AP_M68882_BIAS_EXTENDED;
  const int after = rebiased + (carried ? 1 : 0);
  if (after >= (int)max_exponent) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_OVFL;
    return assemble(f, sign, max_exponent, 0u);
  }
  if (after <= 0) {
    /* Still subnormal: the exponent field is zero and the significand is
     * shifted down to meet it, the leading one included since it is no longer
     * implied. */
    const unsigned shift = (unsigned)(1 - after);
    const uint64_t fraction =
        (rounded.value.mantissa >> (63u - f->fraction_bits)) >> shift;
    return assemble(f, sign, 0u, fraction);
  }
  return assemble(f, sign, (uint32_t)after,
                  (rounded.value.mantissa & ~INTEGER_BIT) >>
                      (63u - f->fraction_bits));
}

/* ---------------------------------------------------------------------------
 * The integers
 */

/* The largest magnitude each width holds, as an unsigned magnitude so that the
 * negative limit -- which has no positive counterpart -- is expressible. */
static uint64_t integer_limit(unsigned size, bool negative) {
  const uint64_t positive = (UINT64_C(1) << (size * 8u - 1u)) - 1u;
  return negative ? positive + 1u : positive;
}

static uint64_t encode_integer(unsigned size, const ap_m68882_extended_t *value,
                               ap_m68882_rounding_t mode,
                               uint32_t *exceptions) {
  const unsigned bits = size * 8u;
  const uint64_t mask = (bits >= 64u) ? UINT64_MAX
                                      : ((UINT64_C(1) << bits) - 1u);

  switch (ap_m68882_classify(value)) {
  case AP_M68882_TYPE_NAN: {
    /* Two rules that agree on the result and differ on the exception. §6.1.2
     * for a signalling NAN: "the 8, 16, or 32 most significant bits of the SNAN
     * significand, with the SNAN bit set, are written to the destination."
     * §6.1.3 for a quiet one, via Table 6-2's "Source is Non-Signaling NAN":
     * the same slice, and an operand error. */
    uint64_t mantissa = value->mantissa;
    if (ap_m68882_is_signalling_nan(value)) {
      *exceptions |= UINT32_C(1) << AP_M68882_EXC_SNAN;
      mantissa |= QUIET_BIT;
    } else {
      *exceptions |= UINT32_C(1) << AP_M68882_EXC_OPERR;
    }
    return (mantissa >> (64u - bits)) & mask;
  }
  case AP_M68882_TYPE_INFINITY:
    /* Table 6-2 lists it, and §6.1.3 gives the result: "if the floating-point
     * data register to be stored contains infinity, the result is the largest
     * positive or negative integer that can fit in the specified destination
     * format size". */
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_OPERR;
    const uint64_t limit = integer_limit(size, value->sign);
    return (value->sign ? (UINT64_C(0) - limit) : limit) & mask;
  case AP_M68882_TYPE_ZERO:
    return 0u;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  /* "Rounded to the destination format precision" for an integer destination is
   * rounding to an integer, which is `FINT`'s own job -- reused rather than
   * rewritten, so the mode-following behaviour has one implementation and one
   * set of tests. */
  const ap_m68882_op_t integral = ap_m68882_int(value, mode);
  *exceptions |= integral.exceptions;

  const int exponent =
      (int)integral.value.exponent - AP_M68882_BIAS_EXTENDED;
  if (ap_m68882_classify(&integral.value) == AP_M68882_TYPE_ZERO) {
    return 0u;
  }

  if (exponent < 0) {
    /* `FINT` already rounded to an integer, so a magnitude below one is a
     * rounded-to-zero result and not an overflow. Signed: round-to-minus-
     * infinity turns -0.5 into -1, which has exponent 0 and does not land
     * here, while -0.4 becomes -0 and does. */
    return 0u;
  }

  /* An exponent of 64 or more cannot be shifted into a 64-bit magnitude at all,
   * and every such value is far outside even a long word. Caught before the
   * shift rather than after it, since a shift of 64 is undefined -- the same
   * trap that once made `e^0.5` return infinity. */
  bool overflowed = exponent > 63;
  uint64_t magnitude = 0;
  if (!overflowed) {
    magnitude = integral.value.mantissa >> (63 - exponent);
    overflowed = magnitude > integer_limit(size, integral.value.sign);
  }

  if (overflowed) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_OPERR;
    magnitude = integer_limit(size, integral.value.sign);
  }
  const uint64_t signed_value =
      integral.value.sign ? (UINT64_C(0) - magnitude) : magnitude;
  return signed_value & mask;
}

/* ---------------------------------------------------------------------------
 */

bool ap_m68882_store_encode(ap_m68882_format_t format,
                            const ap_m68882_extended_t *value,
                            ap_m68882_rounding_t mode, int k_factor,
                            ap_m68882_store_t *out) {
  *out = (ap_m68882_store_t){.size = ap_m68882_format_size(format)};

  static const real_format_t single = {8u, 23u, AP_M68882_BIAS_SINGLE, 4u};
  static const real_format_t doubled = {11u, 52u, AP_M68882_BIAS_DOUBLE, 8u};

  switch (format) {
  case AP_M68882_FORMAT_BYTE:
  case AP_M68882_FORMAT_WORD:
  case AP_M68882_FORMAT_LONG:
    put_big_endian(out->bytes, out->size,
                   encode_integer(out->size, value, mode, &out->exceptions));
    return true;

  case AP_M68882_FORMAT_SINGLE:
    put_big_endian(out->bytes, 4u,
                   encode_real(&single, value, mode, &out->exceptions));
    return true;

  case AP_M68882_FORMAT_DOUBLE:
    put_big_endian(out->bytes, 8u,
                   encode_real(&doubled, value, mode, &out->exceptions));
    return true;

  case AP_M68882_FORMAT_EXTENDED: {
    /* The destination *is* the internal format, so there is nothing to round
     * and no range to leave -- the one store that cannot be inexact. A
     * signalling NAN is still made quiet and still raises, because §6.1.2 lists
     * X alongside S and D. */
    ap_m68882_extended_t stored = *value;
    if (ap_m68882_is_signalling_nan(&stored)) {
      out->exceptions |= UINT32_C(1) << AP_M68882_EXC_SNAN;
      stored.mantissa |= QUIET_BIT;
    }
    uint32_t high = 0;
    uint64_t mantissa = 0;
    ap_m68882_to_extended(&stored, &high, &mantissa);
    put_big_endian(out->bytes, 4u, high);
    put_big_endian(out->bytes + 4, 8u, mantissa);
    return true;
  }

  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    /* The two rows differ only in where the k-factor came from -- the
     * instruction or a data register -- and the main processor has already
     * resolved that by the time it gets here. */
    ap_m68882_packed_encode(value, k_factor, mode, out->bytes,
                            &out->exceptions);
    return true;
  }
  return false;
}
