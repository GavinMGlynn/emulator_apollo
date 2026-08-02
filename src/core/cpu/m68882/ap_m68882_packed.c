/* MC68882 packed decimal real. See ap_m68882_packed.h for the format, the type
 * table, and why this needs exact integer arithmetic rather than an
 * approximation. */

#include "cpu/m68882/ap_m68882_packed.h"

#include "cpu/m68882/ap_m68882_format.h"
#include "cpu/m68882/ap_m68882_round.h"

#define INTEGER_BIT (UINT64_C(1) << 63)

/* ---------------------------------------------------------------------------
 * Just enough big integer
 *
 * `5^999` is 2322 bits, and the widest intermediate is that plus the room a
 * division needs above it. 160 limbs is 5120 bits, comfortably clear of both.
 *
 * Limbs are 32 bits so that a multiply-accumulate fits a 64-bit product without
 * needing 128-bit arithmetic, which is a compiler extension this core does not
 * use -- the same reason the square root writes its own.
 */
#define BN_LIMBS 160u

typedef struct {
  uint32_t limb[BN_LIMBS]; /* little endian: limb[0] is least significant */
  unsigned used;           /* limbs in use; zero means the value is zero */
} bignum_t;

static void bn_set(bignum_t *a, uint64_t value) {
  for (unsigned i = 0; i < BN_LIMBS; i++) {
    a->limb[i] = 0;
  }
  a->limb[0] = (uint32_t)value;
  a->limb[1] = (uint32_t)(value >> 32);
  a->used = (value >> 32) != 0u ? 2u : (value != 0u ? 1u : 0u);
}

static void bn_mul_small(bignum_t *a, uint32_t multiplier) {
  uint64_t carry = 0;
  for (unsigned i = 0; i < a->used; i++) {
    const uint64_t product = (uint64_t)a->limb[i] * multiplier + carry;
    a->limb[i] = (uint32_t)product;
    carry = product >> 32;
  }
  while (carry != 0u && a->used < BN_LIMBS) {
    a->limb[a->used] = (uint32_t)carry;
    carry >>= 32;
    a->used++;
  }
}

static unsigned bn_bitlength(const bignum_t *a) {
  if (a->used == 0u) {
    return 0u;
  }
  unsigned bits = a->used * 32u;
  uint32_t top = a->limb[a->used - 1u];
  while ((top & 0x80000000u) == 0u) {
    top <<= 1;
    bits--;
  }
  return bits;
}

static void bn_shift_left(bignum_t *a, unsigned bits) {
  if (a->used == 0u || bits == 0u) {
    return;
  }
  const unsigned words = bits / 32u;
  const unsigned rest = bits % 32u;
  for (unsigned i = BN_LIMBS; i-- > 0;) {
    uint32_t value = 0;
    if (i >= words) {
      value = a->limb[i - words];
      if (rest != 0u) {
        value = (uint32_t)(value << rest);
        if (i > words) {
          value |= a->limb[i - words - 1u] >> (32u - rest);
        }
      }
    }
    a->limb[i] = value;
  }
  a->used = BN_LIMBS;
  while (a->used > 0u && a->limb[a->used - 1u] == 0u) {
    a->used--;
  }
}

/* Whether `a` is at least `b`. */
static bool bn_at_least(const bignum_t *a, const bignum_t *b) {
  if (a->used != b->used) {
    return a->used > b->used;
  }
  for (unsigned i = a->used; i-- > 0;) {
    if (a->limb[i] != b->limb[i]) {
      return a->limb[i] > b->limb[i];
    }
  }
  return true;
}

static void bn_subtract(bignum_t *a, const bignum_t *b) {
  uint64_t borrow = 0;
  for (unsigned i = 0; i < a->used; i++) {
    const uint64_t right = (i < b->used ? b->limb[i] : 0u) + borrow;
    const uint64_t left = a->limb[i];
    a->limb[i] = (uint32_t)(left - right);
    borrow = left < right ? 1u : 0u;
  }
  while (a->used > 0u && a->limb[a->used - 1u] == 0u) {
    a->used--;
  }
}

/* Whether any bit at or below `bit` is set, which is the sticky an extraction
 * discards. */
static bool bn_any_below(const bignum_t *a, unsigned bit) {
  for (unsigned i = 0; i < bit && i < BN_LIMBS * 32u; i++) {
    if ((a->limb[i / 32u] & (UINT32_C(1) << (i % 32u))) != 0u) {
      return true;
    }
  }
  return false;
}

static bool bn_bit(const bignum_t *a, unsigned bit) {
  if (bit >= BN_LIMBS * 32u) {
    return false;
  }
  return (a->limb[bit / 32u] & (UINT32_C(1) << (bit % 32u))) != 0u;
}

/* Multiply by `5^power`, in the largest chunks a 32-bit limb multiplier holds:
 * `5^13` is 1220703125, just under 2^31. */
static void bn_mul_pow5(bignum_t *a, unsigned power) {
  while (power >= 13u) {
    bn_mul_small(a, 1220703125u);
    power -= 13u;
  }
  static const uint32_t small[13] = {1u,      5u,      25u,     125u,   625u,
                                     3125u,   15625u,  78125u,  390625u,
                                     1953125u, 9765625u, 48828125u, 244140625u};
  if (power != 0u) {
    bn_mul_small(a, small[power]);
  }
}

/* Divide `numerator` by `denominator`, bit by bit, keeping the quotient's top
 * `AP_QUOTIENT_BITS` and whether anything was left over.
 *
 * Bit-serial rather than estimated: the quotient wanted here is only about 65
 * bits wide, so the schoolbook shift-and-compare is both short enough and free
 * of the quotient-digit estimation that makes long division easy to get subtly
 * wrong -- and a subtly wrong division here would move the last bit of the
 * result, which is exactly the bit `INEX1` is about. */
static void bn_divide(const bignum_t *numerator, const bignum_t *denominator,
                      bignum_t *quotient, bool *remainder_nonzero) {
  bignum_t remainder;
  bn_set(&remainder, 0u);
  bn_set(quotient, 0u);

  const unsigned bits = bn_bitlength(numerator);
  for (unsigned i = bits; i-- > 0;) {
    bn_shift_left(&remainder, 1u);
    if (bn_bit(numerator, i)) {
      if (remainder.used == 0u) {
        remainder.used = 1u;
      }
      remainder.limb[0] |= 1u;
    }
    bn_shift_left(quotient, 1u);
    if (bn_at_least(&remainder, denominator) && denominator->used != 0u) {
      bn_subtract(&remainder, denominator);
      if (quotient->used == 0u) {
        quotient->used = 1u;
      }
      quotient->limb[0] |= 1u;
    }
  }
  *remainder_nonzero = remainder.used != 0u;
}

/* The top 64 bits of `a`, with the bit below them as the guard and everything
 * lower folded into the sticky. Returns the bit length, which is what fixes the
 * binary exponent. */
static unsigned bn_top(const bignum_t *a, uint64_t *mantissa, bool *guard,
                       bool *sticky) {
  const unsigned bits = bn_bitlength(a);
  *mantissa = 0;
  *guard = false;
  if (bits == 0u) {
    return 0u;
  }
  for (unsigned i = 0; i < 64u; i++) {
    /* Bit `bits - 1 - i` of the value becomes bit `63 - i` of the mantissa. */
    if (bits > i && bn_bit(a, bits - 1u - i)) {
      *mantissa |= UINT64_C(1) << (63u - i);
    }
  }
  if (bits > 64u) {
    *guard = bn_bit(a, bits - 65u);
    *sticky = *sticky || (bits > 65u && bn_any_below(a, bits - 65u));
  }
  return bits;
}

/* ---------------------------------------------------------------------------
 * The format
 */

static unsigned digit(const uint8_t *bytes, unsigned nibble) {
  /* Nibble 0 is the most significant of byte 0, counting up. */
  const uint8_t byte = bytes[nibble / 2u];
  return (nibble % 2u == 0u) ? (unsigned)(byte >> 4) : (unsigned)(byte & 0x0Fu);
}

void ap_m68882_packed_decode(const uint8_t *bytes, ap_m68882_rounding_t mode,
                             ap_m68882_extended_t *out, uint32_t *exceptions) {
  const bool sign = (bytes[0] & 0x80u) != 0u;
  const bool exponent_sign = (bytes[0] & 0x40u) != 0u;
  const bool y_bits_set = (bytes[0] & 0x30u) == 0x30u;

  /* The three exponent digits, nibbles 1-3. */
  const unsigned exponent_digits[3] = {digit(bytes, 1u), digit(bytes, 2u),
                                       digit(bytes, 3u)};

  /* Table 3-4's infinity and NAN rows: `SE` and both `y` bits set *and* an
   * exponent of `$FFF`. All three, because the exponent alone is an ordinary
   * in-range one in the `-ZERO` and in-range rows. */
  if (exponent_sign && y_bits_set && exponent_digits[0] == 0xFu &&
      exponent_digits[1] == 0xFu && exponent_digits[2] == 0xFu) {
    /* The sixteen fraction digits are the low sixty-four bits: bytes 4-11, of
     * which the integer digit `MANT16` at nibble 15 is *not* part. */
    uint64_t fraction = 0;
    for (unsigned i = 0; i < 8u; i++) {
      fraction = (fraction << 8) | bytes[4u + i];
    }
    out->sign = sign;
    out->exponent = 0x7FFFu;
    if (fraction == 0u) {
      out->mantissa = INTEGER_BIT; /* infinity */
      return;
    }
    /* "the fraction part of the NAN is moved bit-for-bit into the extended
     * precision mantissa ... no decimal-to-binary conversion or any other
     * conversion is performed", so the payload is copied and the signalling bit
     * arrives already in place. */
    out->mantissa = fraction;
    return;
  }

  /* An in-range string. Seventeen digits, the integer one at nibble 15 and the
   * sixteen fraction digits after it, read as one integer -- which turns the
   * implicit decimal point into a scale of `10^-16` folded into the exponent
   * below. `10^17 - 1` is under `2^57`, so this is exact. */
  /* Twelve bytes are twenty-four nibbles. `MANT16` is bits 67-64, which is the
   * *low* nibble of byte 3 -- nibble 7, not nibble 15: bytes 2 and 3 hold the
   * don't-care field above it and only its bottom four bits are mantissa. The
   * sixteen fraction digits follow through nibble 23. */
  uint64_t mantissa_digits = 0;
  for (unsigned i = 0; i < 17u; i++) {
    mantissa_digits = mantissa_digits * 10u + digit(bytes, 7u + i);
  }

  if (mantissa_digits == 0u) {
    /* Table 3-4's zero rows, which are any exponent at all -- and Note 2's
     * non-decimal exponent digits land here too: "If a non-decimal digit
     * appears in the exponent of a zero, the number is converted to a true
     * zero." */
    out->sign = sign;
    out->exponent = 0u;
    out->mantissa = 0u;
    return;
  }

  /* Note 2 again, for the digits this does *not* police: `$A`-`$F` outside a
   * zero's exponent are "converted to binary in the same manner as decimal
   * digits". The loops above did exactly that, so the result is the useless but
   * repeatable value the part produces rather than a refusal. */
  const int magnitude = (int)(exponent_digits[0] * 100u +
                              exponent_digits[1] * 10u + exponent_digits[2]);
  const int exponent =
      (exponent_sign ? -magnitude : magnitude) - 16; /* the implicit point */

  bignum_t value;
  bn_set(&value, mantissa_digits);

  uint64_t mantissa = 0;
  bool guard = false;
  bool sticky = false;
  int binary_exponent = 0;

  if (exponent >= 0) {
    /* `M x 10^E` is `M x 5^E` scaled by `2^E`, and the first factor is exact. */
    bn_mul_pow5(&value, (unsigned)exponent);
    const unsigned bits = bn_top(&value, &mantissa, &guard, &sticky);
    binary_exponent = (int)bits - 1 + exponent;
  } else {
    /* `M / 5^k` scaled by `2^E`. The numerator is shifted up first so the
     * quotient lands at 65 bits: one more than the mantissa keeps, so the guard
     * is a real bit of the quotient rather than a rounding of it. */
    bignum_t denominator;
    bn_set(&denominator, 1u);
    bn_mul_pow5(&denominator, (unsigned)(-exponent));

    const unsigned numerator_bits = bn_bitlength(&value);
    const unsigned denominator_bits = bn_bitlength(&denominator);
    const unsigned shift =
        (unsigned)(65 + (int)denominator_bits - (int)numerator_bits);
    bn_shift_left(&value, shift);

    bignum_t quotient;
    bool leftover = false;
    bn_divide(&value, &denominator, &quotient, &leftover);
    sticky = leftover;

    const unsigned bits = bn_top(&quotient, &mantissa, &guard, &sticky);
    binary_exponent = (int)bits - 1 - (int)shift + exponent;
  }

  /* Note 3: "Since in-range numbers cannot overflow or underflow when converted
   * to extended precision, normalized extended precision numbers are always
   * produced", so there is no range check here and nothing but `INEX1` to
   * raise. */
  ap_m68882_extended_t assembled = {
      .sign = sign,
      .exponent = (uint16_t)(AP_M68882_BIAS_EXTENDED + binary_exponent),
      .mantissa = mantissa};

  /* §6.1.8: rounded "to extended precision (regardless of FPSR mode byte
   * rounding precision)" -- so the precision is fixed here and only the mode is
   * the program's. The round bit is folded into the sticky, which leaves a tie
   * as guard set with nothing below it: the same condition Figure 6-3 tests. */
  const ap_m68882_round_result_t rounded = ap_m68882_round_to_bits(
      assembled, guard, false, sticky, mode, 64u);
  *out = rounded.value;
  if (rounded.inexact) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_INEX1;
  }
}
