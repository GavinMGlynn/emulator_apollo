/* MC68882 §4.3.2's exponential family. Semantics and the accuracy argument are
 * in the header; this file is the arithmetic. */

#include "cpu/m68882/ap_m68882_transcendental.h"

#include <stddef.h>

#include "cpu/m68882/ap_m68882_format.h"

/* ---------------------------------------------------------------------------
 * Constants, generated to 80 decimal digits and rounded once to extended.
 *
 * `c_ln2_hi` keeps only the top 32 mantissa bits so that `n * c_ln2_hi` is
 * **exact** for every integer `n` the exponent range can produce: `n` needs at
 * most 15 bits and 15 + 32 is well inside 64. That exactness is the whole point
 * of the split -- it is what makes `x - n*ln2` lose nothing, which is where a
 * naive exponential throws away half its accuracy.
 * ------------------------------------------------------------------------- */

static const ap_m68882_extended_t c_ln2_hi = {false, 0x3FFE,
                                              0xB17217F700000000ULL};
static const ap_m68882_extended_t c_ln2_lo = {false, 0x3FDE,
                                              0xD1CF79ABC9E3B398ULL};
static const ap_m68882_extended_t c_log2e = {false, 0x3FFF,
                                             0xB8AA3B295C17F0BCULL};
static const ap_m68882_extended_t c_log2_10 = {false, 0x4000,
                                               0xD49A784BCD1B8AFEULL};
/* `log2(10)` does not fit in 64 bits, and for `FTENTOX` that matters: the
 * reduction multiplies by it and then splits off an integer part as large as
 * 16384, so the constant's own rounding error is magnified into an absolute
 * error in the fractional part. This is what is left over -- about 2^-65
 * relative -- and adding `x * c_log2_10_lo` back into the residual takes the
 * reduction from three thousand units in the last place to under one.
 *
 * Worth stating plainly because it is a trap that an *exact* product hides: a
 * product computed exactly is still only exact in the constant it was given. */
static const ap_m68882_extended_t c_log2_10_lo = {false, 0x3FBF,
                                                  0x9257EDFE9B5FB69AULL};
static const ap_m68882_extended_t c_one = {false, 0x3FFF,
                                           0x8000000000000000ULL};
static const ap_m68882_extended_t c_two = {false, 0x4000,
                                           0x8000000000000000ULL};
/* `log10(2)`, split like `ln2` and for the same reason: `FLOG10` multiplies it
 * by an exponent as large as 16384, and the low half keeps that product from
 * inheriting the constant's rounding error. */
static const ap_m68882_extended_t c_log10_2_hi = {false, 0x3FFD,
                                                  0x9A209A8400000000ULL};
static const ap_m68882_extended_t c_log10_2_lo = {false, 0x3FDD,
                                                  0xFBCFF7988F8959ACULL};
static const ap_m68882_extended_t c_ln10_recip = {false, 0x3FFD,
                                                  0xDE5BD8A937287195ULL};

/* The mantissa of the square root of two, which is where the logarithm's
 * reduction splits: bringing the significand into `[1/sqrt2, sqrt2)` rather
 * than `[1, 2)` halves the series argument and is what lets fourteen terms
 * reach below one unit in the last place instead of thirty. */
#define SQRT2_MANTISSA 0xB504F333F9DE6484ULL

/* `1/(k+1)!` for k = 0..17: the coefficients of `(e^r - 1)/r`.
 *
 * Eighteen terms because the reduced argument satisfies `|r| <= ln2/2`, where
 * the first omitted term is `r^18/19!` -- about 2.3e-27, some seven orders of
 * magnitude below one unit in the last place of extended precision. The series
 * is truncated where it stops mattering rather than where it stops being
 * convenient. */
static const ap_m68882_extended_t expm1_coefficients[] = {
    {false, 0x3FFF, 0x8000000000000000ULL}, /* 1/1! */
    {false, 0x3FFE, 0x8000000000000000ULL}, /* 1/2! */
    {false, 0x3FFC, 0xAAAAAAAAAAAAAAABULL}, /* 1/3! */
    {false, 0x3FFA, 0xAAAAAAAAAAAAAAABULL}, /* 1/4! */
    {false, 0x3FF8, 0x8888888888888889ULL}, /* 1/5! */
    {false, 0x3FF5, 0xB60B60B60B60B60BULL}, /* 1/6! */
    {false, 0x3FF2, 0xD00D00D00D00D00DULL}, /* 1/7! */
    {false, 0x3FEF, 0xD00D00D00D00D00DULL}, /* 1/8! */
    {false, 0x3FEC, 0xB8EF1D2AB6399C7DULL}, /* 1/9! */
    {false, 0x3FE9, 0x93F27DBBC4FAE397ULL}, /* 1/10! */
    {false, 0x3FE5, 0xD7322B3FAA271C7FULL}, /* 1/11! */
    {false, 0x3FE2, 0x8F76C77FC6C4BDAAULL}, /* 1/12! */
    {false, 0x3FDE, 0xB092309D43684BE5ULL}, /* 1/13! */
    {false, 0x3FDA, 0xC9CBA54603E4E906ULL}, /* 1/14! */
    {false, 0x3FD6, 0xD73F9F399DC0F88FULL}, /* 1/15! */
    {false, 0x3FD2, 0xD73F9F399DC0F88FULL}, /* 1/16! */
    {false, 0x3FCE, 0xCA963B81856A5359ULL}, /* 1/17! */
    {false, 0x3FCA, 0xB413C31DCBECBBDEULL}, /* 1/18! */
};

#define EXPM1_TERMS (sizeof expm1_coefficients / sizeof expm1_coefficients[0])

/* `2/(2k+1)`: the coefficients of `2 atanh(s) = ln((1+s)/(1-s))`.
 *
 * The logarithm is computed through `atanh` rather than from a series in
 * `m - 1`, because the substitution `s = (m-1)/(m+1)` maps the reduced
 * significand's whole range onto `|s| <= 0.1716` and the series has only odd
 * powers. Fourteen terms put the first omitted one at about 1.6e-22, four
 * orders of magnitude below a unit in the last place. */
static const ap_m68882_extended_t log_coefficients[] = {
    {false, 0x4000, 0x8000000000000000ULL}, /* 2/1 */
    {false, 0x3FFE, 0xAAAAAAAAAAAAAAABULL}, /* 2/3 */
    {false, 0x3FFD, 0xCCCCCCCCCCCCCCCDULL}, /* 2/5 */
    {false, 0x3FFD, 0x9249249249249249ULL}, /* 2/7 */
    {false, 0x3FFC, 0xE38E38E38E38E38EULL}, /* 2/9 */
    {false, 0x3FFC, 0xBA2E8BA2E8BA2E8CULL}, /* 2/11 */
    {false, 0x3FFC, 0x9D89D89D89D89D8AULL}, /* 2/13 */
    {false, 0x3FFC, 0x8888888888888889ULL}, /* 2/15 */
    {false, 0x3FFB, 0xF0F0F0F0F0F0F0F1ULL}, /* 2/17 */
    {false, 0x3FFB, 0xD79435E50D79435EULL}, /* 2/19 */
    {false, 0x3FFB, 0xC30C30C30C30C30CULL}, /* 2/21 */
    {false, 0x3FFB, 0xB21642C8590B2164ULL}, /* 2/23 */
    {false, 0x3FFB, 0xA3D70A3D70A3D70AULL}, /* 2/25 */
    {false, 0x3FFB, 0x97B425ED097B425FULL}, /* 2/27 */
};

#define LOG_TERMS (sizeof log_coefficients / sizeof log_coefficients[0])

/* Pi over two, to about 199 bits in three exactly-representable pieces.
 *
 * One 64-bit `pi/2` is nowhere near enough. The trigonometric reduction
 * subtracts `n * pi/2` from an argument whose own magnitude sets how large `n`
 * gets, and the constant's truncation error is multiplied by `n`: at
 * `n = 2^63` a 64-bit `pi/2` leaves an absolute error of about 0.8 radians,
 * which is not an inaccurate answer but a meaningless one.
 *
 * Three pieces put the residual at about `2^-199`, so the reduction stays
 * accurate while `n` fits in a 64-bit significand -- arguments up to roughly
 * `1.4e19`. Beyond that it degrades, which is what the part does too: the
 * `FSIN` page says "large arguments may lose accuracy during reduction, and
 * very large arguments (greater than approximately 10^20) lose all accuracy".
 * The threshold this model reaches and the one the manual quotes are within a
 * factor of ten of each other, so the degradation is modelled rather than
 * merely tolerated. */
static const ap_m68882_extended_t c_pio2_1 = {false, 0x3FFF,
                                              0xC90FDAA22168C235ULL};
static const ap_m68882_extended_t c_pio2_2 = {true, 0x3FBD,
                                              0xECE675D1FC8F8CBBULL};
static const ap_m68882_extended_t c_pio2_3 = {true, 0x3F7C,
                                              0xB7ED8FBBACC19C60ULL};
static const ap_m68882_extended_t c_two_over_pi = {false, 0x3FFE,
                                                   0xA2F9836E4E44152AULL};

/* `(-1)^k/(2k+1)!` and `(-1)^k/(2k)!`: the sine and cosine series. Twelve terms
 * each, where the reduced argument satisfies `|r| <= pi/4` and the first
 * omitted terms are about 1.5e-28 and 4.9e-27 -- both far below a unit in the
 * last place. */
static const ap_m68882_extended_t sin_coefficients[] = {
    {false, 0x3FFF, 0x8000000000000000ULL},
    {true, 0x3FFC, 0xAAAAAAAAAAAAAAABULL},
    {false, 0x3FF8, 0x8888888888888889ULL},
    {true, 0x3FF2, 0xD00D00D00D00D00DULL},
    {false, 0x3FEC, 0xB8EF1D2AB6399C7DULL},
    {true, 0x3FE5, 0xD7322B3FAA271C7FULL},
    {false, 0x3FDE, 0xB092309D43684BE5ULL},
    {true, 0x3FD6, 0xD73F9F399DC0F88FULL},
    {false, 0x3FCE, 0xCA963B81856A5359ULL},
    {true, 0x3FC6, 0x97A4DA340A0AB926ULL},
    {false, 0x3FBD, 0xB8DC77B6E7AB8C5FULL},
    {true, 0x3FB4, 0xBB0DA098B1C0CECCULL},
};

static const ap_m68882_extended_t cos_coefficients[] = {
    {false, 0x3FFF, 0x8000000000000000ULL},
    {true, 0x3FFE, 0x8000000000000000ULL},
    {false, 0x3FFA, 0xAAAAAAAAAAAAAAABULL},
    {true, 0x3FF5, 0xB60B60B60B60B60BULL},
    {false, 0x3FEF, 0xD00D00D00D00D00DULL},
    {true, 0x3FE9, 0x93F27DBBC4FAE397ULL},
    {false, 0x3FE2, 0x8F76C77FC6C4BDAAULL},
    {true, 0x3FDA, 0xC9CBA54603E4E906ULL},
    {false, 0x3FD2, 0xD73F9F399DC0F88FULL},
    {true, 0x3FCA, 0xB413C31DCBECBBDEULL},
    {false, 0x3FC1, 0xF2A15D201011283DULL},
    {true, 0x3FB9, 0x8671CB6DBFC294A3ULL},
};

#define TRIG_TERMS (sizeof sin_coefficients / sizeof sin_coefficients[0])

static const ap_m68882_extended_t c_pi = {false, 0x4000, 0xC90FDAA22168C235ULL};
static const ap_m68882_extended_t c_pio2 = {false, 0x3FFF,
                                            0xC90FDAA22168C235ULL};
static const ap_m68882_extended_t c_pio4 = {false, 0x3FFE,
                                            0xC90FDAA22168C235ULL};
/* `tan(pi/8) = sqrt(2) - 1`, the point at which the arc tangent's second
 * reduction pays for itself. */
static const ap_m68882_extended_t c_tan_pio8 = {false, 0x3FFD,
                                                0xD413CCCFE7799211ULL};

/* `(-1)^k/(2k+1)`: the arc tangent series.
 *
 * Sixteen terms, which is only enough because the argument is reduced twice
 * before it arrives -- to `|t| <= tan(pi/8)` by the addition formula and then
 * to `|u| <= 0.1989` by the half-angle identity. Unreduced, the same accuracy
 * at `|t| = 1` would need thousands of terms, since this series converges more
 * slowly than any other in this file. */
static const ap_m68882_extended_t atan_coefficients[] = {
    {false, 0x3FFF, 0x8000000000000000ULL}, /* +1/1 */
    {true, 0x3FFD, 0xAAAAAAAAAAAAAAABULL},  /* -1/3 */
    {false, 0x3FFC, 0xCCCCCCCCCCCCCCCDULL}, /* +1/5 */
    {true, 0x3FFC, 0x9249249249249249ULL},  /* -1/7 */
    {false, 0x3FFB, 0xE38E38E38E38E38EULL}, /* +1/9 */
    {true, 0x3FFB, 0xBA2E8BA2E8BA2E8CULL},  /* -1/11 */
    {false, 0x3FFB, 0x9D89D89D89D89D8AULL}, /* +1/13 */
    {true, 0x3FFB, 0x8888888888888889ULL},  /* -1/15 */
    {false, 0x3FFA, 0xF0F0F0F0F0F0F0F1ULL}, /* +1/17 */
    {true, 0x3FFA, 0xD79435E50D79435EULL},  /* -1/19 */
    {false, 0x3FFA, 0xC30C30C30C30C30CULL}, /* +1/21 */
    {true, 0x3FFA, 0xB21642C8590B2164ULL},  /* -1/23 */
    {false, 0x3FFA, 0xA3D70A3D70A3D70AULL}, /* +1/25 */
    {true, 0x3FFA, 0x97B425ED097B425FULL},  /* -1/27 */
    {false, 0x3FFA, 0x8D3DCB08D3DCB08DULL}, /* +1/29 */
    {true, 0x3FFA, 0x8421084210842108ULL},  /* -1/31 */
};

#define ATAN_TERMS (sizeof atan_coefficients / sizeof atan_coefficients[0])

/* ---------------------------------------------------------------------------
 * Working arithmetic.
 *
 * Every intermediate is computed at round-to-nearest, extended precision,
 * whatever the caller asked for: the FPCR's rounding mode and precision apply
 * to the *result*, not to the steps that produce it. Rounding each step to
 * single precision and then rounding the answer again would be double rounding
 * on a grand scale, and would make `FSIN` at single precision materially worse
 * than `FSIN` at extended -- which is not how the part behaves and not what
 * §6.1's rounding section describes.
 * ------------------------------------------------------------------------- */

static ap_m68882_extended_t nx_add(ap_m68882_extended_t a,
                                   ap_m68882_extended_t b) {
  return ap_m68882_add(&a, &b, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED)
      .value;
}

static ap_m68882_extended_t nx_sub(ap_m68882_extended_t a,
                                   ap_m68882_extended_t b) {
  return ap_m68882_sub(&a, &b, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED)
      .value;
}

static ap_m68882_extended_t nx_mul(ap_m68882_extended_t a,
                                   ap_m68882_extended_t b) {
  return ap_m68882_mul(&a, &b, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED)
      .value;
}

static ap_m68882_extended_t nx_div(ap_m68882_extended_t a,
                                   ap_m68882_extended_t b) {
  return ap_m68882_div(&a, &b, AP_M68882_ROUND_NEAREST,
                       AP_M68882_PRECISION_EXTENDED)
      .value;
}

static ap_m68882_extended_t nx_zero(bool sign) {
  return (ap_m68882_extended_t){sign, 0u, 0u};
}

static ap_m68882_extended_t nx_infinity(bool sign) {
  return (ap_m68882_extended_t){sign, 0x7FFFu, 0u};
}

/* The non-signalling NAN §6.1.3 gives as the trap-disabled result. */
static ap_m68882_extended_t nx_nan(void) {
  return (ap_m68882_extended_t){false, 0x7FFFu, 0xFFFFFFFFFFFFFFFFULL};
}

/* Multiply by a power of two by adjusting the exponent. Exact where it fits,
 * and saturating to an infinity or a zero where it does not -- which is the
 * overflow and underflow the caller has to report. */
static ap_m68882_extended_t nx_scale2(ap_m68882_extended_t v, int n,
                                      bool *overflow, bool *underflow) {
  if (v.mantissa == 0u || v.exponent == 0x7FFFu) {
    return v;
  }
  const int e = (int)v.exponent + n;
  if (e >= 0x7FFF) {
    *overflow = true;
    return nx_infinity(v.sign);
  }
  if (e < 0) {
    /* Gradual underflow rather than flush to zero: "the result mantissa is
     * shifted right (denormalized) while the result exponent is incremented
     * until the result exponent reaches the minimum value".
     *
     * Flushing here cost `FSINH` and `FATANH` their whole answer for a denormal
     * argument, because both halve a value at the end and a halved denormal was
     * being thrown away rather than denormalised.
     *
     * The test is `< 0` and not `<= 0` for the reason the multiply gives:
     * exponent zero is a legal extended exponent, not the first one below the
     * range. */
    const unsigned shift = (unsigned)(-e);
    if (shift >= 64u) {
      *underflow = true;
      return nx_zero(v.sign);
    }
    const uint64_t lost = v.mantissa & ((1ULL << shift) - 1u);
    v.mantissa >>= shift;
    v.exponent = 0u;
    if (lost != 0u || (v.mantissa & (1ULL << 63)) == 0u) {
      *underflow = true;
    }
    return v;
  }
  v.exponent = (uint16_t)e;
  return v;
}

/* Round to the nearest integer, returned as an `int` because every use is an
 * exponent-sized quantity. Values beyond the exponent range saturate, and the
 * caller has already decided the result overflows by then. */
static int nx_round_to_int(ap_m68882_extended_t v) {
  if (v.mantissa == 0u) {
    return 0;
  }
  const int e = (int)v.exponent - AP_M68882_BIAS_EXTENDED;
  if (e < -1) {
    return 0; /* |v| < 1/2 */
  }
  if (e > 20) {
    return v.sign ? -(1 << 20) : (1 << 20);
  }
  /* Shift the mantissa so the integer part is in the low bits, then round half
   * away from zero -- which for the exact halves this sees (an integer plus a
   * half only arises from `FTWOTOX`'s reduction) keeps `|f| <= 1/2`.
   *
   * `e == -1` -- a magnitude in `[1/2, 1)` -- makes the shift 64, which is not
   * a shift a `uint64_t` can take. It is handled as its own case rather than
   * clamped: there the whole part is zero by definition and the entire mantissa
   * is the fraction, with the halfway point at bit 63. Getting this wrong is
   * how `FETOX` came to return an infinity for `e^0.5`, because the undefined
   * shift left a wild integer for the exponent scaling to apply. */
  const int shift = 63 - e;
  uint64_t whole, fraction, half;
  if (shift >= 64) {
    whole = 0u;
    fraction = v.mantissa;
    half = 1ULL << 63;
  } else {
    whole = v.mantissa >> shift;
    fraction = v.mantissa & ((1ULL << shift) - 1u);
    half = 1ULL << (shift - 1);
  }
  if (fraction >= half) {
    whole++;
  }
  return v.sign ? -(int)whole : (int)whole;
}

static ap_m68882_extended_t nx_from_int(int n) {
  if (n == 0) {
    return nx_zero(false);
  }
  const bool sign = n < 0;
  uint64_t magnitude = (uint64_t)(sign ? -(long long)n : (long long)n);
  int e = 63;
  while ((magnitude & (1ULL << 63)) == 0u) {
    magnitude <<= 1;
    e--;
  }
  return (ap_m68882_extended_t){sign, (uint16_t)(AP_M68882_BIAS_EXTENDED + e),
                                magnitude};
}

/* Split a value so that `hi` holds the top 32 mantissa bits and `lo` the rest.
 * Both are exact, and `hi + lo == v` exactly. */
static void nx_split(ap_m68882_extended_t v, ap_m68882_extended_t *hi,
                     ap_m68882_extended_t *lo) {
  ap_m68882_extended_t h = v;
  h.mantissa &= 0xFFFFFFFF00000000ULL;
  *hi = h;
  *lo = nx_sub(v, h);
}

/* An exact product as an unevaluated sum: `hi + lo == a * b` with no rounding
 * error at all.
 *
 * Splitting each operand at 32 bits makes all four cross products exactly
 * representable -- 32 bits times 32 bits is 64 -- so the only question is
 * summing them without loss, and they are added smallest-first for that reason.
 * `FTENTOX` needs this: its reduction multiplies by `log2(10)` and the product
 * can reach 16384, where a single rounded multiply would leave an absolute
 * error of `2^-50` in an argument that then goes into an exponential. That is
 * roughly three thousand units in the last place of the answer -- inside
 * §4.3.2's worst case but far outside its typical bound, which is not good
 * enough when the fix is this cheap. */
static void nx_exact_mul(ap_m68882_extended_t a, ap_m68882_extended_t b,
                         ap_m68882_extended_t *hi, ap_m68882_extended_t *lo) {
  ap_m68882_extended_t a_hi, a_lo, b_hi, b_lo;
  nx_split(a, &a_hi, &a_lo);
  nx_split(b, &b_hi, &b_lo);

  const ap_m68882_extended_t p = nx_mul(a, b);
  /* err = a*b - p, accumulated from the exact cross products. */
  ap_m68882_extended_t err = nx_sub(nx_mul(a_hi, b_hi), p);
  err = nx_add(err, nx_mul(a_hi, b_lo));
  err = nx_add(err, nx_mul(a_lo, b_hi));
  err = nx_add(err, nx_mul(a_lo, b_lo));
  *hi = p;
  *lo = err;
}

/* `(e^r - 1)` for `|r| <= ln2/2`, by Horner on the coefficient table.
 *
 * Evaluated as `r * P(r)` rather than as a series in `r` directly, so that the
 * leading `r` is a single exact multiply at the end and the polynomial itself
 * is a value near one -- which keeps every partial sum well scaled and stops
 * the small-argument case losing bits to a sum that starts at zero. */
static ap_m68882_extended_t expm1_kernel(ap_m68882_extended_t r) {
  ap_m68882_extended_t acc = expm1_coefficients[EXPM1_TERMS - 1u];
  for (size_t i = EXPM1_TERMS - 1u; i > 0u; i--) {
    acc = nx_add(expm1_coefficients[i - 1u], nx_mul(acc, r));
  }
  return nx_mul(r, acc);
}

/* ---------------------------------------------------------------------------
 * The shared reduction.
 *
 * `e^x = 2^n * e^r` with `n = round(x * log2 e)` and `r = x - n ln2`, the `r`
 * formed against the split `ln2` so the cancellation is exact.
 * ------------------------------------------------------------------------- */

static ap_m68882_extended_t exp_reduced(ap_m68882_extended_t x, int *n_out) {
  const int n = nx_round_to_int(nx_mul(x, c_log2e));
  const ap_m68882_extended_t n_value = nx_from_int(n);
  /* `n * c_ln2_hi` is exact, and `x` and it are within `ln2` of each other, so
   * this subtraction is exact too: the difference of two nearby values of the
   * same sign loses nothing. */
  ap_m68882_extended_t r = nx_sub(x, nx_mul(n_value, c_ln2_hi));
  r = nx_sub(r, nx_mul(n_value, c_ln2_lo));
  *n_out = n;
  return r;
}

/* The result of an exponential once the reduction has been done: `2^n` times
 * `1 + expm1(r)`, rounded once to the caller's mode and precision. */
static ap_m68882_op_t exp_finish(ap_m68882_extended_t r, int n,
                                 ap_m68882_rounding_t mode,
                                 ap_m68882_precision_t precision) {
  const ap_m68882_extended_t value = nx_add(c_one, expm1_kernel(r));
  bool overflow = false, underflow = false;
  const ap_m68882_extended_t scaled =
      nx_scale2(value, n, &overflow, &underflow);
  if (overflow) {
    /* §6.1.4's result is mode-dependent and is not always an infinity, so the
     * saturation `nx_scale2` performs is only a marker: the value it produced
     * is discarded and the documented one substituted. Rounding the saturated
     * infinity instead would make round-to-zero return an infinity, which the
     * part never does. */
    ap_m68882_op_t out = {ap_m68882_overflow_result(scaled.sign, mode,
                                                    precision),
                          (1u << AP_M68882_EXC_OVFL) |
                              (1u << AP_M68882_EXC_INEX2)};
    return out;
  }
  /* One rounding, at the end, from the extended intermediate -- which is what
   * §6.1's rounding discussion requires and what stops a single-precision
   * result being rounded twice. */
  const ap_m68882_extended_t one = scaled;
  ap_m68882_op_t out = ap_m68882_mul(&one, &c_one, mode, precision);
  if (underflow) {
    out.exceptions |= 1u << AP_M68882_EXC_UNFL;
  }
  out.exceptions |= 1u << AP_M68882_EXC_INEX2;
  return out;
}

/* Every function in the family answers the same four questions first. Table 6-2
 * gives no operand error for any of them: a NAN propagates, and the infinities
 * and the zero have defined values rather than being errors. */
static bool exp_special(const ap_m68882_extended_t *x, bool zero_is_one,
                        bool negative_infinity_is_zero, ap_m68882_op_t *out) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    *out = (ap_m68882_op_t){nx_nan(),
                            ap_m68882_is_signalling_nan(x)
                                ? (1u << AP_M68882_EXC_SNAN)
                                : 0u};
    return true;
  case AP_M68882_TYPE_INFINITY:
    if (x->sign && negative_infinity_is_zero) {
      *out = (ap_m68882_op_t){nx_zero(false), 0u};
    } else if (x->sign) {
      *out = (ap_m68882_op_t){nx_infinity(true), 0u};
    } else {
      *out = (ap_m68882_op_t){nx_infinity(false), 0u};
    }
    return true;
  case AP_M68882_TYPE_ZERO:
    /* "the exponential functions check for a zero input value" -- the one
     * special case §4.3.2 says the part does test, and the only one where it
     * returns an exact result. */
    *out = (ap_m68882_op_t){zero_is_one ? c_one : nx_zero(x->sign), 0u};
    return true;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  return false;
}

ap_m68882_op_t ap_m68882_etox(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (exp_special(x, true, true, &special)) {
    return special;
  }
  int n = 0;
  const ap_m68882_extended_t r = exp_reduced(*x, &n);
  return exp_finish(r, n, mode, precision);
}

ap_m68882_op_t ap_m68882_etoxm1(const ap_m68882_extended_t *x,
                                ap_m68882_rounding_t mode,
                                ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (exp_special(x, false, false, &special)) {
    /* `e^-inf - 1` is `-1`, not the zero the plain exponential gives. */
    if (ap_m68882_classify(x) == AP_M68882_TYPE_INFINITY && x->sign) {
      ap_m68882_extended_t minus_one = c_one;
      minus_one.sign = true;
      special.value = minus_one;
    }
    return special;
  }
  /* Below `ln2/2` the kernel is the answer and no reduction is wanted: this is
   * the case the instruction exists for, and subtracting one from `e^x` here
   * would cancel away everything the kernel just computed. */
  const int e = (int)x->exponent - AP_M68882_BIAS_EXTENDED;
  if (e <= -2) {
    ap_m68882_extended_t value = expm1_kernel(*x);
    ap_m68882_op_t out = ap_m68882_mul(&value, &c_one, mode, precision);
    out.exceptions |= 1u << AP_M68882_EXC_INEX2;
    return out;
  }
  int n = 0;
  const ap_m68882_extended_t r = exp_reduced(*x, &n);
  bool overflow = false, underflow = false;
  const ap_m68882_extended_t scaled =
      nx_scale2(nx_add(c_one, expm1_kernel(r)), n, &overflow, &underflow);
  if (overflow) {
    return (ap_m68882_op_t){
        ap_m68882_overflow_result(scaled.sign, mode, precision),
        (1u << AP_M68882_EXC_OVFL) | (1u << AP_M68882_EXC_INEX2)};
  }
  ap_m68882_extended_t value = nx_sub(scaled, c_one);
  ap_m68882_op_t out = ap_m68882_mul(&value, &c_one, mode, precision);
  out.exceptions |= 1u << AP_M68882_EXC_INEX2;
  return out;
}

ap_m68882_op_t ap_m68882_twotox(const ap_m68882_extended_t *x,
                                ap_m68882_rounding_t mode,
                                ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (exp_special(x, true, true, &special)) {
    return special;
  }
  /* `n` is exact and `f = x - n` is exact by cancellation, so the only rounding
   * in the reduction is the single multiply by `ln2`. */
  const int n = nx_round_to_int(*x);
  const ap_m68882_extended_t f = nx_sub(*x, nx_from_int(n));
  ap_m68882_extended_t r = nx_mul(f, c_ln2_hi);
  r = nx_add(r, nx_mul(f, c_ln2_lo));
  return exp_finish(r, n, mode, precision);
}

ap_m68882_op_t ap_m68882_tentox(const ap_m68882_extended_t *x,
                                ap_m68882_rounding_t mode,
                                ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (exp_special(x, true, true, &special)) {
    return special;
  }
  /* `10^x = 2^(x log2 10)`, and the product is formed exactly as a pair so that
   * the integer part can be split off without the fractional part inheriting
   * the product's rounding error. See `nx_exact_mul`. */
  ap_m68882_extended_t y_hi, y_lo;
  nx_exact_mul(*x, c_log2_10, &y_hi, &y_lo);
  /* The exact product is exact in `c_log2_10`, which is not `log2(10)`. */
  y_lo = nx_add(y_lo, nx_mul(*x, c_log2_10_lo));
  const int n = nx_round_to_int(y_hi);
  ap_m68882_extended_t f = nx_sub(y_hi, nx_from_int(n));
  f = nx_add(f, y_lo);
  ap_m68882_extended_t r = nx_mul(f, c_ln2_hi);
  r = nx_add(r, nx_mul(f, c_ln2_lo));
  return exp_finish(r, n, mode, precision);
}

/* ---------------------------------------------------------------------------
 * The logarithms.
 * ------------------------------------------------------------------------- */

/* `ln(m)` for `m` in `[1/sqrt2, sqrt2)`, as `2 atanh(s)` with
 * `s = (m - 1)/(m + 1)`.
 *
 * `m - 1` is exact over that whole range by Sterbenz's rule, so the
 * substitution costs nothing even when `m` is a hair from one -- which is the
 * case a series in `m - 1` would handle worst and this one handles best. */
static ap_m68882_extended_t log_kernel(ap_m68882_extended_t m) {
  const ap_m68882_extended_t s =
      nx_div(nx_sub(m, c_one), nx_add(m, c_one));
  const ap_m68882_extended_t s2 = nx_mul(s, s);
  ap_m68882_extended_t acc = log_coefficients[LOG_TERMS - 1u];
  for (size_t i = LOG_TERMS - 1u; i > 0u; i--) {
    acc = nx_add(log_coefficients[i - 1u], nx_mul(acc, s2));
  }
  return nx_mul(s, acc);
}

/* Split a finite positive value into `m * 2^k` with `m` in `[1/sqrt2, sqrt2)`.
 * Denormals are normalised first, which is why `k` can run below the exponent
 * field's range. */
static ap_m68882_extended_t log_reduce(ap_m68882_extended_t x, int *k_out) {
  int k = (int)x.exponent - AP_M68882_BIAS_EXTENDED;
  uint64_t mantissa = x.mantissa;
  if (x.exponent == 0u) {
    /* A denormal: shift up until the integer bit is set, and pay for it in the
     * exponent. Skipping this would take the logarithm of a significand that
     * is not in `[1, 2)` at all. */
    k = 1 - AP_M68882_BIAS_EXTENDED;
    while ((mantissa & (1ULL << 63)) == 0u) {
      mantissa <<= 1;
      k--;
    }
  }
  if (mantissa >= SQRT2_MANTISSA) {
    /* Halving the significand costs nothing -- it is an exponent adjustment --
     * and brings `s` back inside the series' range. */
    k += 1;
    *k_out = k;
    return (ap_m68882_extended_t){false,
                                  (uint16_t)(AP_M68882_BIAS_EXTENDED - 1),
                                  mantissa};
  }
  *k_out = k;
  return (ap_m68882_extended_t){false, AP_M68882_BIAS_EXTENDED, mantissa};
}

/* Table 6-2 and the `FLOGN` operation table, page 4-56: "this function is not
 * defined for input values less than zero". A negative source is an operand
 * error; a zero is a divide by zero returning a negative infinity; a positive
 * infinity is itself. Returns true when the answer is one of those. */
static bool log_special(const ap_m68882_extended_t *x, ap_m68882_op_t *out) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    *out = (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
    return true;
  case AP_M68882_TYPE_ZERO:
    /* "Set if the source is (+ or -)0" -- both signs of zero, and the result is
     * a negative infinity rather than a NAN. `FLOGNP1` differs here, which is
     * the trap that module's own guard records. */
    *out = (ap_m68882_op_t){nx_infinity(true), 1u << AP_M68882_EXC_DZ};
    return true;
  case AP_M68882_TYPE_INFINITY:
    *out = x->sign ? (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR}
                   : (ap_m68882_op_t){nx_infinity(false), 0u};
    return true;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    if (x->sign) {
      *out = (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
      return true;
    }
    break;
  }
  return false;
}

/* `ln(x)` as an extended value, with the reduction's exponent term formed
 * against the split `ln2` so a large exponent costs nothing. */
static ap_m68882_extended_t logn_value(ap_m68882_extended_t x) {
  int k = 0;
  const ap_m68882_extended_t m = log_reduce(x, &k);
  const ap_m68882_extended_t kv = nx_from_int(k);
  ap_m68882_extended_t out = nx_add(nx_mul(kv, c_ln2_hi), log_kernel(m));
  return nx_add(out, nx_mul(kv, c_ln2_lo));
}

/* Round once, at the end, to the caller's mode and precision. `INEX2` is raised
 * because a logarithm of anything but an exact power of the base is irrational
 * -- and the exact cases are the ones that return before reaching here. */
static ap_m68882_op_t log_finish(ap_m68882_extended_t value,
                                 ap_m68882_rounding_t mode,
                                 ap_m68882_precision_t precision,
                                 bool inexact) {
  ap_m68882_op_t out = ap_m68882_mul(&value, &c_one, mode, precision);
  if (inexact) {
    out.exceptions |= 1u << AP_M68882_EXC_INEX2;
  }
  return out;
}

ap_m68882_op_t ap_m68882_logn(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (log_special(x, &special)) {
    return special;
  }
  /* `ln(1)` is exactly zero and raises nothing: the reduction gives `k = 0` and
   * a kernel argument of zero, so this falls out rather than being special
   * cased -- but the exactness has to survive the final rounding, which is why
   * `INEX2` is conditional. */
  const bool exact = x->exponent == AP_M68882_BIAS_EXTENDED &&
                     x->mantissa == 0x8000000000000000ULL;
  return log_finish(logn_value(*x), mode, precision, !exact);
}

ap_m68882_op_t ap_m68882_log2(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (log_special(x, &special)) {
    return special;
  }
  /* Not `ln(x)/ln2`: the exponent stays an exact integer and only the
   * significand's contribution is scaled, so a power of two comes out as its
   * exponent with no error at all. Dividing at the end would round the integer
   * along with everything else. */
  int k = 0;
  const ap_m68882_extended_t m = log_reduce(*x, &k);
  const bool exact = m.mantissa == 0x8000000000000000ULL;
  const ap_m68882_extended_t value =
      nx_add(nx_from_int(k), nx_mul(log_kernel(m), c_log2e));
  return log_finish(value, mode, precision, !exact);
}

ap_m68882_op_t ap_m68882_log10(const ap_m68882_extended_t *x,
                               ap_m68882_rounding_t mode,
                               ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (log_special(x, &special)) {
    return special;
  }
  /* `k log10(2) + ln(m)/ln10`, with `log10(2)` split so the exponent term does
   * not inherit a rounding error the significand term cannot cancel. */
  int k = 0;
  const ap_m68882_extended_t m = log_reduce(*x, &k);
  const ap_m68882_extended_t kv = nx_from_int(k);
  ap_m68882_extended_t value =
      nx_add(nx_mul(kv, c_log10_2_hi), nx_mul(log_kernel(m), c_ln10_recip));
  value = nx_add(value, nx_mul(kv, c_log10_2_lo));
  return log_finish(value, mode, precision, true);
}

ap_m68882_op_t ap_m68882_lognp1(const ap_m68882_extended_t *x,
                                ap_m68882_rounding_t mode,
                                ap_m68882_precision_t precision) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    return (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
  case AP_M68882_TYPE_ZERO:
    /* "+0.0" and "-0.0" in the operation table: the sign is kept, and nothing
     * is raised. */
    return (ap_m68882_op_t){*x, 0u};
  case AP_M68882_TYPE_INFINITY:
    return x->sign
               ? (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR}
               : (ap_m68882_op_t){nx_infinity(false), 0u};
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  if (x->sign) {
    /* The page-4-58 note, and it does **not** mirror `FLOGN`'s zero case: "if
     * the source is -1, sets the DZ bit in the FPSR exception byte and returns
     * a NAN. If the source is < -1, sets the OPERR bit ... and returns a NAN."
     *
     * So a divide by zero here yields a NAN where `FLOGN(0)` yields a negative
     * infinity, for the same mathematical singularity. Modelling the two alike
     * would return an infinity from an instruction the manual says returns a
     * NAN, and the difference is invisible until a program compares against
     * one. */
    if (x->exponent == AP_M68882_BIAS_EXTENDED &&
        x->mantissa == 0x8000000000000000ULL) {
      return (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_DZ};
    }
    if (x->exponent > AP_M68882_BIAS_EXTENDED ||
        (x->exponent == AP_M68882_BIAS_EXTENDED &&
         x->mantissa > 0x8000000000000000ULL)) {
      return (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
    }
  }

  /* `ln(1+x) = 2 atanh(x/(x+2))`, an identity rather than an approximation.
   * Below a quarter it is used directly, because `1 + x` would round away the
   * argument's low bits and there is no cancellation in `x + 2`. That is the
   * whole reason the instruction exists separately from `FLOGN`.
   *
   * The threshold is a quarter and not a half, and the difference matters. What
   * bounds this path is not where `1 + x` starts rounding but where the series
   * stops converging fast enough: `s = x/(x+2)` reaches `-1/3` at `x = -1/2`,
   * twice the `0.1716` the fourteen coefficients were chosen for, and the
   * truncation error there is some four thousand units in the last place. At a
   * quarter, `s` stays inside `[-0.1429, 0.1111]` and the first omitted term is
   * negligible. Above it, `1 + x` is at least `3/4` and loses at most a couple
   * of bits, so `FLOGN` handles it to within a unit or two. */
  const int e = (int)x->exponent - AP_M68882_BIAS_EXTENDED;
  /* A denormal has an exponent *field* of zero, which is not an exponent of
   * zero -- so testing the field alone excluded the very smallest arguments
   * from the path that exists for small arguments, and sent them through
   * `1 + x` instead, where they vanished. */
  if (x->exponent == 0u || e <= -3) {
    /* Below `2^-64` this series is its first term. The threshold is *not* the
     * arc tangent's, and the difference is the whole point: `ln(1+x)` is
     * `x(1 - x/2 + ...)`, so its correction is `x/2` *relative* to the answer,
     * while `atan(t)` is `t(1 - t^2/3 + ...)` and its correction is `t^2/3`.
     * One needs `x` below `2^-64` to be negligible in a 64-bit significand; the
     * other needs only `2^-32`.
     *
     * Taking the arc tangent's threshold here cost twenty bits: at `2^-43` the
     * correction is `2^-44` relative, which is a million units in the last
     * place, and the accuracy sweep caught it immediately.
     *
     * The guard exists because forming `x/(x+2)` halves the argument, and a
     * halved denormal is gone before the series that would double it back has
     * run. */
    if (x->mantissa != 0u && e < -64) {
      ap_m68882_op_t direct = ap_m68882_mul(x, &c_one, mode, precision);
      direct.exceptions |= 1u << AP_M68882_EXC_INEX2;
      return direct;
    }
    const ap_m68882_extended_t s = nx_div(*x, nx_add(*x, c_two));
    const ap_m68882_extended_t s2 = nx_mul(s, s);
    ap_m68882_extended_t acc = log_coefficients[LOG_TERMS - 1u];
    for (size_t i = LOG_TERMS - 1u; i > 0u; i--) {
      acc = nx_add(log_coefficients[i - 1u], nx_mul(acc, s2));
    }
    return log_finish(nx_mul(s, acc), mode, precision, true);
  }
  return log_finish(logn_value(nx_add(*x, c_one)), mode, precision, true);
}

/* ---------------------------------------------------------------------------
 * The trigonometric functions.
 * ------------------------------------------------------------------------- */

/* Build an extended value from a 64-bit magnitude. */
static ap_m68882_extended_t nx_from_uint64(uint64_t magnitude, bool sign) {
  if (magnitude == 0u) {
    return nx_zero(sign);
  }
  int e = 63;
  while ((magnitude & (1ULL << 63)) == 0u) {
    magnitude <<= 1;
    e--;
  }
  return (ap_m68882_extended_t){sign, (uint16_t)(AP_M68882_BIAS_EXTENDED + e),
                                magnitude};
}

/* Round to the nearest integer, staying in extended precision. Unlike
 * `nx_round_to_int` this has no range limit, which the trigonometric reduction
 * needs: its quotient is the argument divided by `pi/2` and can be far larger
 * than anything an `int` holds. */
static ap_m68882_extended_t nx_round_integer(ap_m68882_extended_t v) {
  if (v.mantissa == 0u || v.exponent == 0x7FFFu) {
    return v;
  }
  const int e = (int)v.exponent - AP_M68882_BIAS_EXTENDED;
  if (e >= 63) {
    return v; /* every mantissa bit is already integral */
  }
  if (e < -1) {
    return nx_zero(v.sign); /* |v| < 1/2 */
  }
  const int shift = 63 - e;
  uint64_t whole, fraction, half;
  if (shift >= 64) {
    whole = 0u;
    fraction = v.mantissa;
    half = 1ULL << 63;
  } else {
    whole = v.mantissa >> shift;
    fraction = v.mantissa & ((1ULL << shift) - 1u);
    half = 1ULL << (shift - 1);
  }
  if (fraction >= half) {
    whole++;
  }
  return nx_from_uint64(whole, v.sign);
}

/* An integral extended value modulo four, as 0..3.
 *
 * Only the two lowest integral bits matter, and where they sit depends on the
 * exponent: at or above `2^65` every value is a multiple of four and the answer
 * is zero without looking at the mantissa at all. */
static unsigned nx_mod4(ap_m68882_extended_t v) {
  if (v.mantissa == 0u) {
    return 0u;
  }
  const int e = (int)v.exponent - AP_M68882_BIAS_EXTENDED;
  unsigned low;
  if (e >= 65) {
    return 0u;
  }
  if (e >= 63) {
    low = (unsigned)((v.mantissa << (unsigned)(e - 63)) & 3u);
  } else {
    low = (unsigned)((v.mantissa >> (unsigned)(63 - e)) & 3u);
  }
  if (v.sign && low != 0u) {
    low = 4u - low;
  }
  return low;
}

/* `x = n*(pi/2) + r` with `|r| <= pi/4`, returning the quadrant `n mod 4`.
 *
 * Each `n * pi/2_i` is formed as an unevaluated exact pair, so the only error
 * in the reduction is the 199-bit truncation of the constant itself. The first
 * subtraction cancels almost everything and is exact for that reason; the rest
 * are ordinary. */
static unsigned trig_reduce(ap_m68882_extended_t x,
                            ap_m68882_extended_t *r_out) {
  const ap_m68882_extended_t n = nx_round_integer(nx_mul(x, c_two_over_pi));
  const ap_m68882_extended_t parts[3] = {c_pio2_1, c_pio2_2, c_pio2_3};
  ap_m68882_extended_t r = x;
  for (unsigned i = 0; i < 3u; i++) {
    ap_m68882_extended_t hi, lo;
    nx_exact_mul(n, parts[i], &hi, &lo);
    r = nx_sub(r, hi);
    r = nx_sub(r, lo);
  }
  *r_out = r;
  return nx_mod4(n);
}

/* `sin(r)` for `|r| <= pi/4`, as `r * P(r^2)`. */
static ap_m68882_extended_t sin_kernel(ap_m68882_extended_t r) {
  const ap_m68882_extended_t r2 = nx_mul(r, r);
  ap_m68882_extended_t acc = sin_coefficients[TRIG_TERMS - 1u];
  for (size_t i = TRIG_TERMS - 1u; i > 0u; i--) {
    acc = nx_add(sin_coefficients[i - 1u], nx_mul(acc, r2));
  }
  return nx_mul(r, acc);
}

/* `cos(r)` for `|r| <= pi/4`, as `Q(r^2)`.
 *
 * There is no cancellation to worry about here: over that range `cos` stays in
 * `[0.707, 1]`, so the leading one never has to be recovered from a difference
 * of nearly equal terms. */
static ap_m68882_extended_t cos_kernel(ap_m68882_extended_t r) {
  const ap_m68882_extended_t r2 = nx_mul(r, r);
  ap_m68882_extended_t acc = cos_coefficients[TRIG_TERMS - 1u];
  for (size_t i = TRIG_TERMS - 1u; i > 0u; i--) {
    acc = nx_add(cos_coefficients[i - 1u], nx_mul(acc, r2));
  }
  return acc;
}

static ap_m68882_extended_t nx_negate(ap_m68882_extended_t v) {
  v.sign = !v.sign;
  return v;
}

/* The operation tables for `FSIN`, `FCOS` and `FTAN` all agree on the two
 * special cases: a zero passes through with its sign (a cosine returning one
 * instead), and an infinity of either sign is an operand error. None of the
 * three has a divide by zero -- `FTAN` at `pi/2` is a *finite* number, because
 * `pi/2` is not representable. */
static bool trig_special(const ap_m68882_extended_t *x, bool zero_is_one,
                         ap_m68882_op_t *out) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    *out = (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
    return true;
  case AP_M68882_TYPE_INFINITY:
    *out = (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
    return true;
  case AP_M68882_TYPE_ZERO:
    *out = (ap_m68882_op_t){zero_is_one ? c_one : *x, 0u};
    return true;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  return false;
}

static ap_m68882_op_t trig_finish(ap_m68882_extended_t value,
                                  ap_m68882_rounding_t mode,
                                  ap_m68882_precision_t precision) {
  ap_m68882_op_t out = ap_m68882_mul(&value, &c_one, mode, precision);
  out.exceptions |= 1u << AP_M68882_EXC_INEX2;
  return out;
}

/* Both results at once, which is what makes `FSINCOS` a single instruction
 * rather than two: the reduction is the expensive part and it is shared. */
static void sincos_value(ap_m68882_extended_t x, ap_m68882_extended_t *sine,
                         ap_m68882_extended_t *cosine) {
  ap_m68882_extended_t r;
  const unsigned quadrant = trig_reduce(x, &r);
  const ap_m68882_extended_t s = sin_kernel(r);
  const ap_m68882_extended_t c = cos_kernel(r);
  switch (quadrant) {
  case 0u:
    *sine = s;
    *cosine = c;
    break;
  case 1u:
    *sine = c;
    *cosine = nx_negate(s);
    break;
  case 2u:
    *sine = nx_negate(s);
    *cosine = nx_negate(c);
    break;
  default:
    *sine = nx_negate(c);
    *cosine = s;
    break;
  }
}

ap_m68882_op_t ap_m68882_sin(const ap_m68882_extended_t *x,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (trig_special(x, false, &special)) {
    return special;
  }
  ap_m68882_extended_t sine, cosine;
  sincos_value(*x, &sine, &cosine);
  return trig_finish(sine, mode, precision);
}

ap_m68882_op_t ap_m68882_cos(const ap_m68882_extended_t *x,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (trig_special(x, true, &special)) {
    return special;
  }
  ap_m68882_extended_t sine, cosine;
  sincos_value(*x, &sine, &cosine);
  return trig_finish(cosine, mode, precision);
}

void ap_m68882_sincos(const ap_m68882_extended_t *x, ap_m68882_rounding_t mode,
                      ap_m68882_precision_t precision, ap_m68882_op_t *sine,
                      ap_m68882_op_t *cosine) {
  ap_m68882_op_t special;
  if (trig_special(x, false, &special)) {
    *sine = special;
    /* The cosine of a zero is one and of an infinity is the same operand
     * error, so only the zero case differs between the two results. */
    *cosine = special;
    if (ap_m68882_classify(x) == AP_M68882_TYPE_ZERO) {
      cosine->value = c_one;
    }
    return;
  }
  ap_m68882_extended_t s, c;
  sincos_value(*x, &s, &c);
  *sine = trig_finish(s, mode, precision);
  *cosine = trig_finish(c, mode, precision);
}

ap_m68882_op_t ap_m68882_tan(const ap_m68882_extended_t *x,
                             ap_m68882_rounding_t mode,
                             ap_m68882_precision_t precision) {
  ap_m68882_op_t special;
  if (trig_special(x, false, &special)) {
    return special;
  }
  ap_m68882_extended_t r;
  const unsigned quadrant = trig_reduce(*x, &r);
  const ap_m68882_extended_t s = sin_kernel(r);
  const ap_m68882_extended_t c = cos_kernel(r);
  /* An odd quadrant is a quarter turn away, where the tangent is the negated
   * reciprocal. Dividing the kernels rather than the reconstructed sine and
   * cosine keeps both operands well scaled. */
  const ap_m68882_extended_t value =
      (quadrant & 1u) ? nx_negate(nx_div(c, s)) : nx_div(s, c);
  return trig_finish(value, mode, precision);
}

/* ---------------------------------------------------------------------------
 * The inverse trigonometric functions.
 * ------------------------------------------------------------------------- */

static ap_m68882_extended_t nx_sqrt(ap_m68882_extended_t v) {
  return ap_m68882_sqrt(&v, AP_M68882_ROUND_NEAREST,
                        AP_M68882_PRECISION_EXTENDED)
      .value;
}

/* `atan(u)` for `|u| <= 0.1989`, straight from the series. */
static ap_m68882_extended_t atan_series(ap_m68882_extended_t u) {
  const ap_m68882_extended_t u2 = nx_mul(u, u);
  ap_m68882_extended_t acc = atan_coefficients[ATAN_TERMS - 1u];
  for (size_t i = ATAN_TERMS - 1u; i > 0u; i--) {
    acc = nx_add(atan_coefficients[i - 1u], nx_mul(acc, u2));
  }
  return nx_mul(u, acc);
}

/* `atan(t)` for `|t| <= tan(pi/8)`, halving the argument once first.
 *
 * `atan(t) = 2 atan(t / (1 + sqrt(1 + t^2)))` takes `0.4142` down to `0.1989`,
 * which is the difference between sixteen series terms and nearly sixty. The
 * square root is worth one divide and one root because the series' convergence
 * is geometric in the argument and every halving buys a great many terms. */
static ap_m68882_extended_t atan_kernel(ap_m68882_extended_t t) {
  /* Below `2^-40` the whole series is its first term: `atan(t) = t - t^3/3 +
   * ...` and `t^3` is already beyond the significand. Returned directly because
   * the half-angle step would otherwise *destroy* such an argument -- halving
   * the smallest denormal gives zero, and the doubling that follows cannot
   * bring it back. The reduction that buys accuracy in the middle of the range
   * is what loses everything at the bottom of it. */
  if (t.mantissa != 0u &&
      (int)t.exponent - AP_M68882_BIAS_EXTENDED < -40) {
    return t;
  }
  const ap_m68882_extended_t root = nx_sqrt(nx_add(c_one, nx_mul(t, t)));
  const ap_m68882_extended_t u = nx_div(t, nx_add(c_one, root));
  return nx_add(atan_series(u), atan_series(u));
}

/* `atan(x)` for any finite `x`, by two range reductions onto the kernel.
 *
 * `atan(x) = pi/2 - atan(1/x)` brings anything above one down; then
 * `atan(t) = pi/4 + atan((t-1)/(t+1))` brings the remaining `(tan(pi/8), 1]`
 * down as well. Neither reduction can cancel badly: the results are near
 * `pi/2` and `pi/4` respectively and the correction is small. */
static ap_m68882_extended_t atan_value(ap_m68882_extended_t x) {
  const bool negative = x.sign;
  ap_m68882_extended_t a = x;
  a.sign = false;

  ap_m68882_extended_t base = nx_zero(false);
  bool subtract = false;
  if (a.exponent > AP_M68882_BIAS_EXTENDED ||
      (a.exponent == AP_M68882_BIAS_EXTENDED &&
       a.mantissa > 0x8000000000000000ULL)) {
    base = c_pio2;
    subtract = true;
    a = nx_div(c_one, a);
  }

  if (a.exponent > c_tan_pio8.exponent ||
      (a.exponent == c_tan_pio8.exponent &&
       a.mantissa > c_tan_pio8.mantissa)) {
    const ap_m68882_extended_t reduced =
        nx_div(nx_sub(a, c_one), nx_add(a, c_one));
    base = subtract ? nx_sub(base, c_pio4) : nx_add(base, c_pio4);
    a = reduced;
  }

  const ap_m68882_extended_t term = atan_kernel(a);
  ap_m68882_extended_t value =
      subtract ? nx_sub(base, term) : nx_add(base, term);
  value.sign ^= negative;
  return value;
}

ap_m68882_op_t ap_m68882_atan(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    return (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
  case AP_M68882_TYPE_ZERO:
    return (ap_m68882_op_t){*x, 0u};
  case AP_M68882_TYPE_INFINITY: {
    /* Unlike the forward functions, an infinite argument is *not* an error
     * here: the arc tangent has horizontal asymptotes, so `atan(+/-inf)` is
     * `+/-pi/2` and the operation table prints it rather than a NAN. */
    ap_m68882_extended_t value = c_pio2;
    value.sign = x->sign;
    return (ap_m68882_op_t){value, 1u << AP_M68882_EXC_INEX2};
  }
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  return trig_finish(atan_value(*x), mode, precision);
}

/* `FASIN` and `FACOS` share a domain and a domain error. Their exception bytes
 * both read "set if the source is infinity, > +1 or < -1", and neither has a
 * divide by zero -- the endpoints are ordinary finite results. */
static bool inverse_domain(const ap_m68882_extended_t *x, ap_m68882_op_t *out) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    *out = (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
    return true;
  case AP_M68882_TYPE_INFINITY:
    *out = (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
    return true;
  case AP_M68882_TYPE_ZERO:
    return false;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  if (x->exponent > AP_M68882_BIAS_EXTENDED ||
      (x->exponent == AP_M68882_BIAS_EXTENDED &&
       x->mantissa > 0x8000000000000000ULL)) {
    *out = (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
    return true;
  }
  return false;
}

static bool is_unit_magnitude(const ap_m68882_extended_t *x) {
  return x->exponent == AP_M68882_BIAS_EXTENDED &&
         x->mantissa == 0x8000000000000000ULL;
}

ap_m68882_op_t ap_m68882_asin(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out;
  if (inverse_domain(x, &out)) {
    return out;
  }
  if (ap_m68882_classify(x) == AP_M68882_TYPE_ZERO) {
    return (ap_m68882_op_t){*x, 0u}; /* asin(+/-0) is +/-0 */
  }
  if (is_unit_magnitude(x)) {
    /* `asin(+/-1)` is `+/-pi/2`. Taken directly rather than through the
     * identity below, whose denominator would be zero here -- and it is *not* a
     * divide by zero in the FPSR sense: the exception byte says `DZ` is
     * cleared, because the value is finite. */
    ap_m68882_extended_t value = c_pio2;
    value.sign = x->sign;
    return trig_finish(value, mode, precision);
  }
  /* `asin(x) = atan(x / sqrt(1 - x^2))`, with `1 - x^2` formed as
   * `(1-x)(1+x)`. Squaring first and subtracting would cancel away most of the
   * significand for `x` near one, which is exactly where the answer is
   * changing fastest. */
  const ap_m68882_extended_t magnitude = {false, x->exponent, x->mantissa};
  const ap_m68882_extended_t product =
      nx_mul(nx_sub(c_one, magnitude), nx_add(c_one, magnitude));
  ap_m68882_extended_t value = atan_value(nx_div(*x, nx_sqrt(product)));
  return trig_finish(value, mode, precision);
}

ap_m68882_op_t ap_m68882_acos(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out;
  if (inverse_domain(x, &out)) {
    return out;
  }
  if (is_unit_magnitude(x)) {
    /* `acos(1)` is exactly zero -- the one exact result this function has --
     * and `acos(-1)` is `pi`. */
    if (x->sign) {
      return trig_finish(c_pi, mode, precision);
    }
    return (ap_m68882_op_t){nx_zero(false), 0u};
  }
  /* `acos(x) = 2 atan(sqrt((1-x)/(1+x)))`, and not `pi/2 - asin(x)`.
   *
   * The subtraction form cancels catastrophically as `x` approaches one, where
   * `asin(x)` approaches `pi/2` and the answer approaches zero: the result
   * would be a difference of two nearly equal numbers and would lose every
   * significant bit exactly where it is smallest. This form produces the small
   * answer directly, because `1 - x` is exact there by Sterbenz's rule. */
  const ap_m68882_extended_t ratio =
      nx_div(nx_sub(c_one, *x), nx_add(c_one, *x));
  const ap_m68882_extended_t half = atan_value(nx_sqrt(ratio));
  return trig_finish(nx_add(half, half), mode, precision);
}

/* ---------------------------------------------------------------------------
 * The hyperbolic functions.
 * ------------------------------------------------------------------------- */

/* Halving is an exponent adjustment, so it is exact and cannot round. */
static ap_m68882_extended_t nx_half(ap_m68882_extended_t v) {
  bool overflow = false, underflow = false;
  return nx_scale2(v, -1, &overflow, &underflow);
}

/* The exponential of an intermediate, at round-to-nearest and extended
 * precision whatever the caller asked for -- the caller's mode applies to the
 * *result*, and an overflow here is reported so the caller can substitute
 * §6.1.4's mode-dependent value rather than propagate this one. */
static ap_m68882_extended_t exp_of(ap_m68882_extended_t x,
                                   uint32_t *exceptions) {
  const ap_m68882_op_t out =
      ap_m68882_etox(&x, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  *exceptions |= out.exceptions & ((1u << AP_M68882_EXC_OVFL) |
                                   (1u << AP_M68882_EXC_UNFL));
  return out.value;
}

/* `FSINH`, `FCOSH` and `FTANH` share an exception byte with `OPERR` cleared:
 * every real argument is in their domain, and an infinity is a limit rather
 * than an error. Only the *values* differ. */
static bool hyperbolic_special(const ap_m68882_extended_t *x, ap_m68882_op_t *out,
                               ap_m68882_extended_t at_zero,
                               ap_m68882_extended_t at_infinity,
                               bool infinity_keeps_sign, bool zero_keeps_sign) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    *out = (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
    return true;
  case AP_M68882_TYPE_ZERO:
    if (zero_keeps_sign) {
      at_zero.sign = x->sign;
    }
    *out = (ap_m68882_op_t){at_zero, 0u};
    return true;
  case AP_M68882_TYPE_INFINITY:
    if (infinity_keeps_sign) {
      at_infinity.sign = x->sign;
    }
    *out = (ap_m68882_op_t){at_infinity, 0u};
    return true;
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }
  return false;
}

ap_m68882_op_t ap_m68882_sinh(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out;
  /* Zero keeps its sign and an infinity keeps its sign: `sinh` is odd. */
  if (hyperbolic_special(x, &out, nx_zero(false), nx_infinity(false), true,
                         true)) {
    return out;
  }
  const bool negative = x->sign;
  ap_m68882_extended_t a = *x;
  a.sign = false;
  uint32_t exceptions = 0u;
  ap_m68882_extended_t value;

  if (a.exponent < AP_M68882_BIAS_EXTENDED) {
    /* Below one, `e^x - e^-x` would cancel: both terms are near one and the
     * answer is near `x`. `u + u/(u+1)` with `u = e^x - 1` is the same
     * quantity with the ones removed algebraically, so nothing has to cancel.
     * Below `2^-32` the answer is `x` to within a unit in the last place
     * anyway, and the expression degenerates gracefully rather than needing
     * its own case. */
    const ap_m68882_op_t u_op = ap_m68882_etoxm1(
        &a, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
    const ap_m68882_extended_t u = u_op.value;
    value = nx_half(nx_add(u, nx_div(u, nx_add(u, c_one))));
  } else {
    /* At or above one, `e^x` is at least `e` and `e^-x` at most `1/e`, so the
     * difference loses nothing and the direct form is both simpler and more
     * accurate than carrying `expm1` through a large argument. */
    ap_m68882_extended_t minus = a;
    minus.sign = true;
    value = nx_half(nx_sub(exp_of(a, &exceptions), exp_of(minus, &exceptions)));
  }

  value.sign ^= negative;
  if ((exceptions & (1u << AP_M68882_EXC_OVFL)) != 0u) {
    return (ap_m68882_op_t){
        ap_m68882_overflow_result(value.sign, mode, precision),
        (1u << AP_M68882_EXC_OVFL) | (1u << AP_M68882_EXC_INEX2)};
  }
  if ((exceptions & (1u << AP_M68882_EXC_OVFL)) != 0u) {
    return (ap_m68882_op_t){
        ap_m68882_overflow_result(value.sign, mode, precision),
        (1u << AP_M68882_EXC_OVFL) | (1u << AP_M68882_EXC_INEX2)};
  }
  ap_m68882_op_t result = ap_m68882_mul(&value, &c_one, mode, precision);
  result.exceptions |= exceptions | (1u << AP_M68882_EXC_INEX2);
  return result;
}

ap_m68882_op_t ap_m68882_cosh(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out;
  /* `cosh` is even, so neither the zero nor the infinity carries a sign: the
   * operation table prints `+1.0` for both zeros and `+inf` for both
   * infinities. */
  if (hyperbolic_special(x, &out, c_one, nx_infinity(false), false, false)) {
    return out;
  }
  ap_m68882_extended_t a = *x;
  a.sign = false;
  ap_m68882_extended_t minus = a;
  minus.sign = true;
  uint32_t exceptions = 0u;
  /* No cancellation anywhere: both terms are positive, so the sum is safe for
   * every argument and one path serves the whole range. */
  ap_m68882_extended_t value =
      nx_half(nx_add(exp_of(a, &exceptions), exp_of(minus, &exceptions)));
  ap_m68882_op_t result = ap_m68882_mul(&value, &c_one, mode, precision);
  result.exceptions |= exceptions | (1u << AP_M68882_EXC_INEX2);
  return result;
}

ap_m68882_op_t ap_m68882_tanh(const ap_m68882_extended_t *x,
                              ap_m68882_rounding_t mode,
                              ap_m68882_precision_t precision) {
  ap_m68882_op_t out;
  /* The operation table's infinity column is `+1.0` and `-1.0`, not an
   * infinity: `tanh` has horizontal asymptotes. */
  if (hyperbolic_special(x, &out, nx_zero(false), c_one, true, true)) {
    return out;
  }
  const bool negative = x->sign;
  ap_m68882_extended_t a = *x;
  a.sign = false;

  /* Beyond about 23, `tanh` differs from one by less than a unit in the last
   * place, and computing it would divide one huge number by another. Returned
   * as exactly one -- which is what rounding the true value gives -- rather
   * than risking an infinity over an infinity. */
  if ((int)a.exponent - AP_M68882_BIAS_EXTENDED >= 5) {
    ap_m68882_extended_t value = c_one;
    value.sign = negative;
    ap_m68882_op_t result = ap_m68882_mul(&value, &c_one, mode, precision);
    result.exceptions |= 1u << AP_M68882_EXC_INEX2;
    return result;
  }

  /* `tanh(x) = u/(u+2)` with `u = e^(2x) - 1`, which needs no cancellation at
   * either end: near zero `u` is about `2x` and the quotient about `x`. */
  bool overflow = false, underflow = false;
  const ap_m68882_extended_t doubled = nx_scale2(a, 1, &overflow, &underflow);
  const ap_m68882_op_t u_op = ap_m68882_etoxm1(
      &doubled, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  ap_m68882_extended_t value =
      nx_div(u_op.value, nx_add(u_op.value, c_two));
  value.sign ^= negative;
  ap_m68882_op_t result = ap_m68882_mul(&value, &c_one, mode, precision);
  result.exceptions |= 1u << AP_M68882_EXC_INEX2;
  return result;
}

ap_m68882_op_t ap_m68882_atanh(const ap_m68882_extended_t *x,
                               ap_m68882_rounding_t mode,
                               ap_m68882_precision_t precision) {
  switch (ap_m68882_classify(x)) {
  case AP_M68882_TYPE_NAN:
    return (ap_m68882_op_t){nx_nan(), ap_m68882_is_signalling_nan(x)
                                          ? (1u << AP_M68882_EXC_SNAN)
                                          : 0u};
  case AP_M68882_TYPE_ZERO:
    return (ap_m68882_op_t){*x, 0u}; /* "+0.0" and "-0.0" */
  case AP_M68882_TYPE_INFINITY:
    /* Its operation table's Infinity column is `NAN` with note 1, "sets the
     * OPERR bit" -- an infinity is outside `(-1 ... +1)` like any other value
     * out of range. */
    return (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
  case AP_M68882_TYPE_NORMALIZED:
  case AP_M68882_TYPE_DENORMALIZED:
    break;
  }

  if (is_unit_magnitude(x)) {
    /* The poles, and **the manual has them the wrong way round.**
     *
     * Page 4-26's description reads: "the result is equal to -infinity or
     * +infinity if the source is equal to +1 or -1, respectively". Taken at
     * face value that maps `+1` to a *negative* infinity.
     *
     * It cannot be right. `atanh` is odd and strictly increasing on
     * `(-1, +1)`, and `atanh(x) = (1/2) ln((1+x)/(1-x))`, whose numerator
     * grows and denominator vanishes as `x` rises to one -- so `atanh(+1)` is
     * `+infinity`. The page's own exception byte and operation table are both
     * consistent with that; only the sentence is transposed.
     *
     * This is the one place in the session where a manual defect is corrected
     * rather than transcribed, and it is corrected because the standing rule's
     * second half is satisfied for once: the mathematics does not merely prove
     * the printed text impossible, it supplies the unique replacement. There is
     * no third assignment of two signs to two arguments. `DZ` is raised either
     * way -- "set if the source is equal to +1 or -1" -- so only the sign of
     * the result is at issue. */
    ap_m68882_extended_t value = nx_infinity(x->sign);
    return (ap_m68882_op_t){value, 1u << AP_M68882_EXC_DZ};
  }

  /* "Set if the source is > +1 or < -1". Both halves are needed: a magnitude
   * just above one has the *same* exponent as one and only a larger
   * significand, so testing the exponent alone lets `1 + 2^-63` through to a
   * computation whose logarithm argument is negative. */
  if (x->exponent > AP_M68882_BIAS_EXTENDED ||
      (x->exponent == AP_M68882_BIAS_EXTENDED &&
       x->mantissa > 0x8000000000000000ULL)) {
    return (ap_m68882_op_t){nx_nan(), 1u << AP_M68882_EXC_OPERR};
  }

  /* `atanh(x) = (1/2) ln1p(2x/(1-x))`, using `FLOGNP1`'s kernel rather than a
   * logarithm of a ratio: near zero the argument `2x/(1-x)` is about `2x` and
   * `ln1p` keeps every bit of it, where `ln((1+x)/(1-x))` would form a ratio
   * near one and throw them away. */
  bool overflow = false, underflow = false;
  const ap_m68882_extended_t doubled = nx_scale2(*x, 1, &overflow, &underflow);
  const ap_m68882_extended_t argument =
      nx_div(doubled, nx_sub(c_one, *x));
  const ap_m68882_op_t log_op = ap_m68882_lognp1(
      &argument, AP_M68882_ROUND_NEAREST, AP_M68882_PRECISION_EXTENDED);
  ap_m68882_extended_t value = nx_half(log_op.value);
  ap_m68882_op_t result = ap_m68882_mul(&value, &c_one, mode, precision);
  result.exceptions |= 1u << AP_M68882_EXC_INEX2;
  return result;
}
