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
  if (e <= 0) {
    /* Denormalising here would need a shift and a sticky bit; the family's
     * callers report underflow and a zero of the right sign, which is what
     * §6.1.5's trap-disabled result is once the value is below the range the
     * exponent field can express. */
    *underflow = true;
    return nx_zero(v.sign);
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
  /* One rounding, at the end, from the extended intermediate -- which is what
   * §6.1's rounding discussion requires and what stops a single-precision
   * result being rounded twice. */
  const ap_m68882_extended_t one = scaled;
  ap_m68882_op_t out = ap_m68882_mul(&one, &c_one, mode, precision);
  if (overflow) {
    out.exceptions |= 1u << AP_M68882_EXC_OVFL;
  }
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
  ap_m68882_extended_t value = nx_sub(scaled, c_one);
  ap_m68882_op_t out = ap_m68882_mul(&value, &c_one, mode, precision);
  if (overflow) {
    out.exceptions |= 1u << AP_M68882_EXC_OVFL;
  }
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
  if (x->exponent != 0u && e <= -3) {
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
