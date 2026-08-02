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
 * The input direction's widest intermediate is `5^999`, 2322 bits. The output
 * direction's is far larger: converting `1e4932` to seventeen digits divides a
 * number of some 11500 bits by another of the same size. 512 limbs is 16384
 * bits, clear of both with room to spare.
 *
 * Limbs are 32 bits so that a multiply-accumulate fits a 64-bit product without
 * needing 128-bit arithmetic, which is a compiler extension this core does not
 * use -- the same reason the square root writes its own.
 */
#define BN_LIMBS 512u

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
  const unsigned denominator_bits = bn_bitlength(denominator);
  /* The first `denominator_bits - 1` iterations of a bit-serial division only
   * shift numerator bits into the remainder without ever producing a quotient
   * bit, because the remainder cannot reach the divisor until it is as wide.
   * Seeding those in one step leaves the loop proportional to the *quotient*'s
   * width -- some 60 bits here -- rather than to the numerator's 11500. */
  unsigned start = bits;
  if (denominator_bits > 1u && bits >= denominator_bits) {
    const unsigned seed = denominator_bits - 1u;
    for (unsigned i = 0; i < seed; i++) {
      bn_shift_left(&remainder, 1u);
      if (bn_bit(numerator, bits - 1u - i)) {
        if (remainder.used == 0u) {
          remainder.used = 1u;
        }
        remainder.limb[0] |= 1u;
      }
    }
    start = bits - seed;
  }

  for (unsigned i = start; i-- > 0;) {
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

/* ---------------------------------------------------------------------------
 * Binary to decimal
 */

static void put_digit(uint8_t *bytes, unsigned nibble, unsigned value) {
  if (nibble % 2u == 0u) {
    bytes[nibble / 2u] =
        (uint8_t)((bytes[nibble / 2u] & 0x0Fu) | ((value & 0xFu) << 4));
  } else {
    bytes[nibble / 2u] =
        (uint8_t)((bytes[nibble / 2u] & 0xF0u) | (value & 0xFu));
  }
}

/* Multiply by `2^power`, which is the shift a division by a power of two is
 * not: kept separate so the caller's intent stays readable. */
static void bn_mul_pow2(bignum_t *a, unsigned power) { bn_shift_left(a, power); }

/* `round(numerator / denominator)` under `mode`, where the quotient is known to
 * fit sixty-four bits. Returns whether anything was discarded. */
static bool bn_rounded_quotient(bignum_t *numerator, const bignum_t *denominator,
                                bool sign, ap_m68882_rounding_t mode,
                                uint64_t *result) {
  bignum_t quotient;
  bool leftover = false;
  bn_divide(numerator, denominator, &quotient, &leftover);

  uint64_t value = 0;
  for (unsigned i = 0; i < 2u && i < quotient.used; i++) {
    value |= (uint64_t)quotient.limb[i] << (32u * i);
  }

  if (leftover) {
    /* The discarded part is a fraction of one unit in the last place, and
     * *which* fraction only matters to round-to-nearest -- so it is compared
     * against half a unit by doubling the remainder rather than by forming it.
     * The other three modes need only whether anything was discarded at all. */
    bool round_up = false;
    switch (mode) {
    case AP_M68882_ROUND_ZERO:
      break;
    case AP_M68882_ROUND_PLUS_INFINITY:
      round_up = !sign;
      break;
    case AP_M68882_ROUND_MINUS_INFINITY:
      round_up = sign;
      break;
    case AP_M68882_ROUND_NEAREST: {
      /* `2 x remainder` against the divisor: greater is up, equal is the tie,
       * and a tie goes to even -- the decimal analogue of Figure 6-3's rule,
       * which is what "the k factor specified is used to locate the decimal
       * rounding boundary" leaves to the mode. */
      /* Recovering the remainder as `numerator - quotient x denominator` is
       * more work than dividing a doubled numerator, which answers the same
       * question directly. */
      bignum_t doubled = *numerator;
      bn_shift_left(&doubled, 1u);
      bignum_t doubled_quotient;
      bool doubled_leftover = false;
      bn_divide(&doubled, denominator, &doubled_quotient, &doubled_leftover);
      uint64_t twice_value = 0;
      for (unsigned i = 0; i < 2u && i < doubled_quotient.used; i++) {
        twice_value |= (uint64_t)doubled_quotient.limb[i] << (32u * i);
      }
      /* `floor(2n/d) - 2 floor(n/d)` is 1 when the remainder is at least half
       * and 0 otherwise, and the leftover of the doubled division tells a tie
       * from a clear majority. */
      const uint64_t half_or_more = twice_value - 2u * value;
      if (half_or_more != 0u) {
        round_up = doubled_leftover || ((value & 1u) != 0u);
      }
      break;
    }
    }
    if (round_up) {
      value++;
    }
  }
  *result = value;
  return leftover;
}

void ap_m68882_packed_encode(const ap_m68882_extended_t *value, int k_factor,
                             ap_m68882_rounding_t mode, uint8_t *bytes,
                             uint32_t *exceptions) {
  for (unsigned i = 0; i < 12u; i++) {
    bytes[i] = 0;
  }
  bytes[0] = value->sign ? 0x80u : 0x00u;

  switch (ap_m68882_classify(value)) {
  case AP_M68882_TYPE_INFINITY:
    /* Table 3-4: `SE` and both `y` bits set, exponent `$FFF`, fraction zero. */
    bytes[0] |= 0x7Fu;
    bytes[1] = 0xFFu;
    return;
  case AP_M68882_TYPE_NAN: {
    /* The same markers, and the mantissa copied bit for bit -- Note 1's rule
     * read backwards, so a NAN survives a round trip through memory. */
    bytes[0] |= 0x7Fu;
    bytes[1] = 0xFFu;
    for (unsigned i = 0; i < 8u; i++) {
      bytes[4u + i] = (uint8_t)(value->mantissa >> (8u * (7u - i)));
    }
    return;
  }
  case AP_M68882_TYPE_ZERO:
    return; /* every digit already zero, and the sign kept */
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  /* "+18 to +63 -- Sets the OPERR bit in the FPSR exception byte, treated as
   * +17." Both halves: the exception *and* a usable result, rather than one or
   * the other. */
  int k = k_factor;
  if (k > 17) {
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_OPERR;
    k = 17;
  }

  /* Normalise, since a denormal's stored exponent understates it. */
  uint64_t significand = value->mantissa;
  int binary_exponent = (int)value->exponent - AP_M68882_BIAS_EXTENDED;
  while ((significand & INTEGER_BIT) == 0u) {
    significand <<= 1;
    binary_exponent--;
  }
  /* The value is `significand x 2^(binary_exponent - 63)`. */
  const int scale = binary_exponent - 63;

  /* An estimate of `floor(log10)` from the binary exponent, corrected below by
   * the digit count the conversion actually produces. `log10(2)` as 30103 over
   * 100000 is exact enough that the correction is never more than one step. */
  int decimal_exponent = (int)(((int64_t)binary_exponent * 30103) / 100000);
  if (binary_exponent < 0 && ((int64_t)binary_exponent * 30103) % 100000 != 0) {
    decimal_exponent--; /* C truncates toward zero; this wants the floor */
  }

  uint64_t digits_value = 0;
  unsigned digits = 1;
  bool inexact = false;

  for (unsigned attempt = 0; attempt < 4u; attempt++) {
    /* "- 64 to 0 -- Indicates the number of significant digit to the right of
     * the decimal point (Fortran 'F' format). +1 to +17 -- Indicates the number
     * of significant digits in the mantissa (Fortran 'E' format)." The second
     * is a count outright; the first depends on where the point falls, which is
     * why it needs the decimal exponent and this loop. */
    int wanted = (k >= 1) ? k : (decimal_exponent + 1 - k);
    if (wanted < 1) {
      wanted = 1;
    }
    if (wanted > 17) {
      wanted = 17;
    }
    digits = (unsigned)wanted;

    /* `round(value x 10^p)` with `p` chosen so the result has `digits` digits.
     * `10^p` is `5^p x 2^p`, and each factor lands on whichever side of the
     * fraction its sign puts it -- so at most one real division is needed, and
     * the powers of two are shifts. */
    const int p = (int)digits - 1 - decimal_exponent;
    const int two = scale + p;

    bignum_t numerator;
    bn_set(&numerator, significand);
    bignum_t denominator;
    bn_set(&denominator, 1u);

    if (p >= 0) {
      bn_mul_pow5(&numerator, (unsigned)p);
    } else {
      bn_mul_pow5(&denominator, (unsigned)(-p));
    }
    if (two >= 0) {
      bn_mul_pow2(&numerator, (unsigned)two);
    } else {
      bn_mul_pow2(&denominator, (unsigned)(-two));
    }

    inexact = bn_rounded_quotient(&numerator, &denominator, value->sign, mode,
                                  &digits_value);

    /* The estimate is confirmed by the answer's width. Rounding can also carry
     * a nine-run over -- 999 becoming 1000 -- which is the same correction. */
    uint64_t limit = 1;
    for (unsigned i = 0; i < digits; i++) {
      limit *= 10u;
    }
    if (digits_value >= limit) {
      decimal_exponent++;
      continue;
    }
    if (digits_value < limit / 10u && digits_value != 0u) {
      decimal_exponent--;
      continue;
    }
    break;
  }

  if (inexact) {
    /* §3.6 sends binary-to-decimal inaccuracy to §6.1.7, which is `INEX2` --
     * `INEX1` is decimal *input* only, and the two are separate bits precisely
     * so a program can tell the directions apart. */
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_INEX2;
  }

  /* The seventeen mantissa nibbles, most significant first, left-aligned so the
   * integer digit is `MANT16` and the unused low digits are zero. */
  uint8_t emitted[17] = {0};
  for (unsigned i = 0; i < digits; i++) {
    emitted[digits - 1u - i] = (uint8_t)(digits_value % 10u);
    digits_value /= 10u;
  }
  for (unsigned i = 0; i < 17u; i++) {
    put_digit(bytes, 7u + i, i < digits ? emitted[i] : 0u);
  }

  /* The exponent, as three BCD digits with its own sign. */
  int magnitude = decimal_exponent < 0 ? -decimal_exponent : decimal_exponent;
  if (decimal_exponent < 0) {
    bytes[0] |= 0x40u;
  }
  if (magnitude > 999) {
    /* "If the magnitude of the rounded decimal result exponent exceeds 999, the
     * FPCP signals an operand error and calculates a fourth exponent digit,
     * which is included in the destination operand" -- at nibble 4, which is
     * the `EXP3` position Figure 3-11 shows and a don't care on the way in. */
    *exceptions |= UINT32_C(1) << AP_M68882_EXC_OPERR;
    put_digit(bytes, 4u, (unsigned)((magnitude / 1000) % 10));
    magnitude %= 1000;
  }
  bytes[0] = (uint8_t)((bytes[0] & 0xF0u) |
                       (unsigned)((magnitude / 100) % 10));
  put_digit(bytes, 2u, (unsigned)((magnitude / 10) % 10));
  put_digit(bytes, 3u, (unsigned)(magnitude % 10));
}
