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
