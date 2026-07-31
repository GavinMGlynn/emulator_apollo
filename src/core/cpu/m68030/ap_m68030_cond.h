/* MC68030 conditional tests.
 *
 * `M68000 Family Programmer's Reference Manual 1992` Table 3-19, the sixteen
 * conditions shared by `Bcc`, `Scc`, `DBcc` and `TRAPcc`.
 *
 * ## The overbars did not survive, and the structure is what recovers them
 *
 * Table 3-19's tests use overbars for logical negation, and the scan loses
 * them: `HI` reads as "C^ Z" where the manual means "not-C and not-Z". The
 * table's own NOTES say what the bars meant -- "N = Logical Not N", and the
 * same for V and Z -- but not where they sat.
 *
 * The encoding recovers it without guessing. The conditions are laid out in
 * **complementary pairs**: T/F, HI/LS, CC/CS, NE/EQ, VC/VS, PL/MI, GE/LT,
 * GT/LE, each pair being encoding `2k` and `2k+1`. So for every possible
 * condition code state, condition `2k` must be exactly the negation of `2k+1`
 * -- and a misplaced overbar breaks that for some CCR value. `cond_suite`
 * checks all sixteen conditions against all thirty-two CCR states, which is the
 * whole space, so the transcription is verified rather than trusted.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_COND_H
#define APOLLO_CPU_M68030_AP_M68030_COND_H

#include <stdbool.h>
#include <stdint.h>

/* Encodings from Table 3-19's own column. */
typedef enum {
  AP_M68030_COND_T = 0x0,  /* True */
  AP_M68030_COND_F = 0x1,  /* False */
  AP_M68030_COND_HI = 0x2, /* High */
  AP_M68030_COND_LS = 0x3, /* Low or Same */
  AP_M68030_COND_CC = 0x4, /* Carry Clear, also HS */
  AP_M68030_COND_CS = 0x5, /* Carry Set, also LO */
  AP_M68030_COND_NE = 0x6, /* Not Equal */
  AP_M68030_COND_EQ = 0x7, /* Equal */
  AP_M68030_COND_VC = 0x8, /* Overflow Clear */
  AP_M68030_COND_VS = 0x9, /* Overflow Set */
  AP_M68030_COND_PL = 0xA, /* Plus */
  AP_M68030_COND_MI = 0xB, /* Minus */
  AP_M68030_COND_GE = 0xC, /* Greater or Equal */
  AP_M68030_COND_LT = 0xD, /* Less Than */
  AP_M68030_COND_GT = 0xE, /* Greater Than */
  AP_M68030_COND_LE = 0xF, /* Less or Equal */
} ap_m68030_cond_t;

/* Evaluate a condition against a condition code register value, using the CCR
 * bit positions `ap_m68030_regs.h` defines. */
[[nodiscard]] bool ap_m68030_condition(ap_m68030_cond_t condition, uint16_t ccr);

/* "*Not available for the Bcc instruction." Encodings 0 and 1 are BRA and BSR
 * in the Bcc encoding space rather than conditions, so a Bcc decoder must not
 * reach them through this table. */
[[nodiscard]] bool ap_m68030_condition_available_to_bcc(
    ap_m68030_cond_t condition);

#endif /* APOLLO_CPU_M68030_AP_M68030_COND_H */
