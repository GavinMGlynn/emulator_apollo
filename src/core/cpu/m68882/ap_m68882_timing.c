/* Table 8-3 and Tables 8-16/8-17, transcribed. See the header for what half of
 * an instruction these numbers are and why. */

#include "cpu/m68882/ap_m68882_timing.h"

/* `[881]` Table 8-3, *MC68882 Overall Execution Times*, the `FPn to FPm`
 * column's **Total**. Read off the page image, row by row; the `H` and `T`
 * columns beside it are the concurrency this core does not model.
 *
 * Four rows of that table are not here and each for a stated reason:
 * `FMOVE to memory` has no register-to-register form (the column prints an
 * em dash) and is `ap_m68882_store_clocks` below; `FMOVECR` reads the constant
 * ROM rather than a register, and its 32 is in the table anyway; and the two
 * `FMOVE to FPn` rows differ by Table 8-3's footnote ** -- 21 either way for
 * the total, so the distinction costs nothing here. */
typedef struct {
  ap_m68882_operation_t operation;
  unsigned clocks;
} operation_time_t;

static const operation_time_t OPERATION_TIMES[] = {
    {AP_M68882_OP_FABS, 38},     {AP_M68882_OP_FACOS, 628},
    {AP_M68882_OP_FADD, 56},     {AP_M68882_OP_FASIN, 584},
    {AP_M68882_OP_FATAN, 406},   {AP_M68882_OP_FATANH, 696},
    {AP_M68882_OP_FCMP, 38},     {AP_M68882_OP_FCOS, 394},
    {AP_M68882_OP_FCOSH, 610},   {AP_M68882_OP_FDIV, 108},
    {AP_M68882_OP_FETOX, 500},   {AP_M68882_OP_FETOXM1, 548},
    {AP_M68882_OP_FGETEXP, 48},  {AP_M68882_OP_FGETMAN, 34},
    {AP_M68882_OP_FINT, 58},     {AP_M68882_OP_FINTRZ, 58},
    {AP_M68882_OP_FLOGN, 528},   {AP_M68882_OP_FLOGNP1, 574},
    {AP_M68882_OP_FLOG10, 584},  {AP_M68882_OP_FLOG2, 584},
    {AP_M68882_OP_FMOD, 75},     {AP_M68882_OP_FMOVE_TO_FPN, 21},
    {AP_M68882_OP_FMUL, 76},     {AP_M68882_OP_FNEG, 38},
    {AP_M68882_OP_FREM, 105},    {AP_M68882_OP_FSCALE, 46},
    {AP_M68882_OP_FSGLDIV, 74},  {AP_M68882_OP_FSGLMUL, 64},
    {AP_M68882_OP_FSIN, 394},    {AP_M68882_OP_FSINCOS, 454},
    {AP_M68882_OP_FSINH, 690},   {AP_M68882_OP_FSQRT, 110},
    {AP_M68882_OP_FSUB, 56},     {AP_M68882_OP_FTAN, 476},
    {AP_M68882_OP_FTANH, 664},   {AP_M68882_OP_FTENTOX, 570},
    {AP_M68882_OP_FTST, 36},     {AP_M68882_OP_FTWOTOX, 570},
};

unsigned ap_m68882_operation_clocks(ap_m68882_operation_t operation) {
  /* `FSINCOS` occupies eight encodings, `$30`-`$37`, whose low three bits name
   * the second destination register. The decode already normalises them to
   * `$30`, so this sees one value and the table holds one row. */
  for (unsigned i = 0;
       i < sizeof(OPERATION_TIMES) / sizeof(OPERATION_TIMES[0]); i++) {
    if (OPERATION_TIMES[i].operation == operation) {
      return OPERATION_TIMES[i].clocks;
    }
  }
  return 0u;
}

unsigned ap_m68882_store_clocks(ap_m68882_format_t format) {
  switch (format) {
  /* Table 8-16, *Output Operand Conversion*, the normalized-source column. */
  case AP_M68882_FORMAT_BYTE:
  case AP_M68882_FORMAT_WORD:
  case AP_M68882_FORMAT_LONG:
    /* "Integer, No Overflow", 50 for a positive source and 52 for a negative.
     * The lower of the two, because Table 8-3's row for an integer destination
     * is 110 and it is the positive case the tables assume throughout. */
    return 50u;
  case AP_M68882_FORMAT_SINGLE:
  case AP_M68882_FORMAT_DOUBLE:
    /* Table 8-16 sends both to Table 8-17, whose first row -- "No Underflow,
     * Overflow or Round Overflow", normalized source -- is 38. */
    return 38u;
  case AP_M68882_FORMAT_EXTENDED:
    /* The destination is the internal format, so there is nothing to convert
     * and 18 is the whole of it. This is the store `ap_m68882_store.c` calls
     * "the one store that cannot be inexact", and the table agrees: it is the
     * only real destination with a single number rather than a sub-table. */
    return 18u;
  case AP_M68882_FORMAT_PACKED:
  case AP_M68882_FORMAT_PACKED_DYNAMIC:
    /* Table 8-16's note 2: "1942 clocks is the typical time required for the
     * conversion, if no overflow occurs. The maximum time is 3674 clocks."
     * Two orders of magnitude above every other format, and the reason
     * Table 8-3's packed column runs to 2006 where its extended column is 50.
     * The typical value, as everywhere else here. */
    return 1942u;
  }
  return 0u;
}
