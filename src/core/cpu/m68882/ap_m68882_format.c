/* MC68882 binary real data formats. See ap_m68882_format.h for the explicit
 * integer bit's consequence and for the extended format's unused sixteen bits.
 */

#include "cpu/m68882/ap_m68882_format.h"

#include "cpu/m68882/ap_m68882_packed.h"

#define EXTENDED_MAX_EXPONENT 0x7FFFu
#define INTEGER_BIT (UINT64_C(1) << 63)
/* The most significant *fraction* bit, which is one below the integer bit. */
#define QUIET_BIT (UINT64_C(1) << 62)

ap_m68882_type_t ap_m68882_classify(const ap_m68882_extended_t *value) {
  if (value->exponent == EXTENDED_MAX_EXPONENT) {
    /* The maximum exponent is infinity or a NAN, told apart by the fraction --
     * an all-zero fraction is infinity however the integer bit reads. The
     * integer bit is deliberately not consulted: real 68881 output sets it on
     * infinities, and a model requiring it clear would classify the part's own
     * results as NANs. */
    return (value->mantissa & ~INTEGER_BIT) == 0u ? AP_M68882_TYPE_INFINITY
                                                  : AP_M68882_TYPE_NAN;
  }

  if (value->mantissa == 0u) {
    /* A zero mantissa at any other exponent is a zero. */
    return AP_M68882_TYPE_ZERO;
  }

  /* **The NOTE, and the whole reason this takes an extended value.** For single
   * and double an exponent of zero *means* denormalized; for extended it does
   * not, because the integer bit is explicit and may be set:
   *
   *   "an extended precision number with an exponent of zero may have an
   *   explicit integer bit equal to one, which results in a normalized number
   *   (even though the exponent is equal to the minimum value)."
   *
   * So it is the integer bit that decides, not the exponent. A classifier
   * carried over from the other two formats calls these denormalized and
   * misreads exactly the numbers the extended format exists to hold. */
  return (value->mantissa & INTEGER_BIT) != 0u ? AP_M68882_TYPE_NORMALIZED
                                               : AP_M68882_TYPE_DENORMALIZED;
}

bool ap_m68882_is_signalling_nan(const ap_m68882_extended_t *value) {
  if (ap_m68882_classify(value) != AP_M68882_TYPE_NAN) {
    return false;
  }
  /* Clear means signalling. The bit is the top *fraction* bit, one below the
   * integer bit -- reading bit 63 instead would call every NAN signalling,
   * since a NAN carries its integer bit set. */
  return (value->mantissa & QUIET_BIT) == 0u;
}

/* Convert a single- or double-precision field pair into extended. Shared
 * because the two differ only in their widths and biases, and writing them
 * twice is how one of them ends up with the other's bias. */
static ap_m68882_extended_t from_ieee(bool sign, uint32_t biased_exponent,
                                      uint64_t fraction,
                                      unsigned exponent_bits,
                                      unsigned fraction_bits, int bias) {
  ap_m68882_extended_t out = {0};
  out.sign = sign;

  const uint32_t max_exponent = (1u << exponent_bits) - 1u;

  if (biased_exponent == max_exponent) {
    /* Infinity or NAN: the extended form uses its own maximum exponent, and the
     * fraction is carried up so a NAN keeps its payload -- which is what makes
     * a NAN survive a round trip through a register. */
    out.exponent = EXTENDED_MAX_EXPONENT;
    out.mantissa = INTEGER_BIT | (fraction << (63u - fraction_bits));
    if (fraction == 0u) {
      out.mantissa = INTEGER_BIT; /* infinity */
    }
    return out;
  }

  if (biased_exponent == 0u) {
    if (fraction == 0u) {
      return out; /* a signed zero: exponent and mantissa both zero */
    }
    /* Denormalized. "If the exponent of a single or double precision number is
     * zero, the number is defined to be denormalized, and the implied integer
     * bit is also a zero" -- and the true exponent is the *minimum* one, which
     * is 1 - bias and not 0 - bias. Using the latter puts every denormal a
     * factor of two out. */
    const int unbiased = 1 - bias;
    out.exponent = (uint16_t)(unbiased + AP_M68882_BIAS_EXTENDED);
    out.mantissa = fraction << (63u - fraction_bits);
    return out;
  }

  /* Normalized: rebias, and make the implied leading one explicit. */
  const int unbiased = (int)biased_exponent - bias;
  out.exponent = (uint16_t)(unbiased + AP_M68882_BIAS_EXTENDED);
  out.mantissa = INTEGER_BIT | (fraction << (63u - fraction_bits));
  return out;
}

ap_m68882_extended_t ap_m68882_from_single(uint32_t bits) {
  return from_ieee((bits >> 31) != 0u, (bits >> 23) & 0xFFu,
                   (uint64_t)(bits & 0x7FFFFFu), 8u, 23u,
                   AP_M68882_BIAS_SINGLE);
}

ap_m68882_extended_t ap_m68882_from_double(uint64_t bits) {
  return from_ieee((bits >> 63) != 0u, (uint32_t)((bits >> 52) & 0x7FFu),
                   bits & UINT64_C(0xFFFFFFFFFFFFF), 11u, 52u,
                   AP_M68882_BIAS_DOUBLE);
}

/* The reverse. Deliberately *without* rounding: §2.2.2 puts rounding under the
 * FPCR's precision control, applied to a computed result, and a converter that
 * rounded on its own would apply it twice. What this does is re-encode a value
 * that already fits. */
static uint64_t to_ieee(const ap_m68882_extended_t *value,
                        unsigned exponent_bits, unsigned fraction_bits,
                        int bias) {
  const uint32_t max_exponent = (1u << exponent_bits) - 1u;
  const uint64_t fraction_mask = (UINT64_C(1) << fraction_bits) - 1u;
  uint64_t out = value->sign ? (UINT64_C(1) << (exponent_bits + fraction_bits))
                             : 0u;

  const ap_m68882_type_t kind = ap_m68882_classify(value);
  switch (kind) {
  case AP_M68882_TYPE_ZERO:
    return out;
  case AP_M68882_TYPE_INFINITY:
    return out | ((uint64_t)max_exponent << fraction_bits);
  case AP_M68882_TYPE_NAN:
    return out | ((uint64_t)max_exponent << fraction_bits) |
           (((value->mantissa & ~INTEGER_BIT) >> (63u - fraction_bits)) &
            fraction_mask) |
           /* A NAN whose payload is entirely in the bits being discarded must
            * not become an infinity, so the quiet bit is forced. Without this a
            * NAN can round-trip into an infinity, which is a different data
            * type with different condition codes. */
           (UINT64_C(1) << (fraction_bits - 1u));
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  const int unbiased = (int)value->exponent - AP_M68882_BIAS_EXTENDED;
  const int rebiased = unbiased + bias;
  if (rebiased <= 0) {
    /* Below the destination's range: denormalized there, or zero. Encoded with
     * a zero exponent, which is what that format means by denormal. */
    return out;
  }
  if (rebiased >= (int)max_exponent) {
    return out | ((uint64_t)max_exponent << fraction_bits); /* infinity */
  }
  return out | ((uint64_t)(uint32_t)rebiased << fraction_bits) |
         (((value->mantissa & ~INTEGER_BIT) >> (63u - fraction_bits)) &
          fraction_mask);
}

uint32_t ap_m68882_to_single(const ap_m68882_extended_t *value) {
  return (uint32_t)to_ieee(value, 8u, 23u, AP_M68882_BIAS_SINGLE);
}

uint64_t ap_m68882_to_double(const ap_m68882_extended_t *value) {
  return to_ieee(value, 11u, 52u, AP_M68882_BIAS_DOUBLE);
}

ap_m68882_extended_t ap_m68882_from_extended(uint32_t high, uint64_t mantissa) {
  /* "96 bits, 80 of which are used": sign at bit 31 of the first long word,
   * the 15-bit exponent at 30-16, and **bits 15-0 unused**. The mantissa is the
   * other two long words entire. */
  ap_m68882_extended_t out = {0};
  out.sign = (high >> 31) != 0u;
  out.exponent = (uint16_t)((high >> 16) & 0x7FFFu);
  out.mantissa = mantissa;
  return out;
}

void ap_m68882_to_extended(const ap_m68882_extended_t *value, uint32_t *high,
                           uint64_t *mantissa) {
  /* The sixteen unused bits are written as zero rather than left alone: they
   * are what a store puts in memory, and a caller that saw whatever was there
   * before could not compare two stores of the same value. */
  *high = (uint32_t)((value->sign ? UINT32_C(1) << 31 : 0u) |
                     ((uint32_t)(value->exponent & 0x7FFFu) << 16));
  *mantissa = value->mantissa;
}

ap_m68882_extended_t ap_m68882_from_integer(int32_t value) {
  ap_m68882_extended_t out = {0};
  if (value == 0) {
    return out; /* +0: sign clear, exponent zero, mantissa zero. */
  }

  out.sign = value < 0;
  /* Negated in unsigned arithmetic. `-(int32_t)INT32_MIN` is undefined and this
   * is the one input that reaches it. */
  uint64_t magnitude = out.sign ? UINT64_C(0) - (uint64_t)(int64_t)value
                                : (uint64_t)(int64_t)value;

  /* Normalize: shift the magnitude up until its top bit lands on the explicit
   * integer bit. The value is `mantissa * 2^(exponent - BIAS - 63)`, so moving
   * the mantissa left by `shift` moves the exponent down by the same. */
  unsigned shift = 0u;
  while ((magnitude & (UINT64_C(1) << 63)) == 0u) {
    magnitude <<= 1;
    shift++;
  }
  out.mantissa = magnitude;
  out.exponent = (uint16_t)(AP_M68882_BIAS_EXTENDED + 63 - (int)shift);
  return out;
}

unsigned ap_m68882_format_size(ap_m68882_format_t format) {
  switch (format) {
  case AP_M68882_FORMAT_BYTE:
    return 1u;
  case AP_M68882_FORMAT_WORD:
    return 2u;
  case AP_M68882_FORMAT_LONG:
  case AP_M68882_FORMAT_SINGLE:
    return 4u;
  case AP_M68882_FORMAT_DOUBLE:
    return 8u;
  case AP_M68882_FORMAT_EXTENDED:
  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    return 12u;
  }
  return 0u;
}

/* Assemble `count` bytes, most significant first. */
static uint64_t big_endian(const uint8_t *bytes, unsigned count) {
  uint64_t value = 0;
  for (unsigned i = 0; i < count; i++) {
    value = (value << 8) | bytes[i];
  }
  return value;
}

bool ap_m68882_operand_decode(ap_m68882_format_t format, const uint8_t *bytes,
                              ap_m68882_rounding_t mode,
                              ap_m68882_extended_t *out,
                              uint32_t *exceptions) {
  switch (format) {
  case AP_M68882_FORMAT_BYTE:
    *out = ap_m68882_from_integer((int32_t)(int8_t)bytes[0]);
    return true;
  case AP_M68882_FORMAT_WORD:
    *out = ap_m68882_from_integer((int32_t)(int16_t)big_endian(bytes, 2u));
    return true;
  case AP_M68882_FORMAT_LONG:
    *out = ap_m68882_from_integer((int32_t)(uint32_t)big_endian(bytes, 4u));
    return true;
  case AP_M68882_FORMAT_SINGLE:
    *out = ap_m68882_from_single((uint32_t)big_endian(bytes, 4u));
    return true;
  case AP_M68882_FORMAT_DOUBLE:
    *out = ap_m68882_from_double(big_endian(bytes, 8u));
    return true;
  case AP_M68882_FORMAT_EXTENDED:
    /* The first long word carries the sign and exponent -- and the sixteen
     * unused bits, which is why the mantissa starts at byte 4 and not at the
     * eightieth bit. */
    *out = ap_m68882_from_extended((uint32_t)big_endian(bytes, 4u),
                                   big_endian(bytes + 4, 8u));
    return true;
  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    /* §3.6's decimal-to-binary conversion, the one source format that is
     * arithmetic rather than field extraction and the one that can be inexact.
     * `$7` reaches here as a *destination* format only, but decoding it the
     * same way costs nothing and keeps the two rows together. */
    ap_m68882_packed_decode(bytes, mode, out, exceptions);
    return true;
  }
  return false;
}
