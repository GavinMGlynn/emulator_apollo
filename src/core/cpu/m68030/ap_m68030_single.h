/* MC68030 family 0100: the single-operand group.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 and each
 * instruction's page. NEGX, CLR, NEG, NOT, TST and TAS, together with the
 * `MOVE to/from SR` and `MOVE to/from CCR` instructions that share their
 * encodings, and ILLEGAL. This completes family `0100`.
 *
 * ## Size field `11` is the escape, again
 *
 * Each of NEGX, CLR, NEG, NOT and TST is `0100 xxx0 ss EA` with `ss` the
 * operand size — and `11` is not a size. That spare encoding carries a
 * different instruction in every one of them:
 *
 *     $40C0  MOVE from SR      $44C0  MOVE to CCR
 *     $42C0  MOVE from CCR     $46C0  MOVE to SR
 *     $4AC0  TAS
 *
 * So the same bit pattern that means "long" one row up means an entirely
 * different instruction here. This is the third distinct place in the 68000
 * encoding where an illegal size selects something else — `ADDQ`'s conditional
 * group and `Bcc`'s displacement escapes being the others — and it is worth
 * treating as a family idiom rather than as five separate special cases.
 *
 * ## Two of these are privileged and two are not, in a way that reads backwards
 *
 * `MOVE to SR` writes the whole status register including the S bit, so it is
 * supervisor-only. `MOVE to CCR` writes only the condition codes and is not.
 * On the 68010 and later `MOVE from SR` also became privileged — a user program
 * that can read S learns whether it is supervised — while `MOVE from CCR`,
 * which the 68000 did not have at all, is unprivileged.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_SINGLE_H
#define APOLLO_CPU_M68030_AP_M68030_SINGLE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

typedef enum {
  AP_M68030_SINGLE_NEGX,
  AP_M68030_SINGLE_CLR,
  AP_M68030_SINGLE_NEG,
  AP_M68030_SINGLE_NOT,
  AP_M68030_SINGLE_TST,
  AP_M68030_SINGLE_TAS,
  AP_M68030_SINGLE_MOVE_FROM_SR,
  AP_M68030_SINGLE_MOVE_FROM_CCR,
  AP_M68030_SINGLE_MOVE_TO_CCR,
  AP_M68030_SINGLE_MOVE_TO_SR,
  AP_M68030_SINGLE_ILLEGAL, /* $4AFC, a deliberate illegal instruction */
  AP_M68030_SINGLE_INVALID,
} ap_m68030_single_kind_t;

typedef struct {
  ap_m68030_single_kind_t kind;
  unsigned size;     /* operand size in bytes for the sized forms, else 0 */
  ap_m68030_ea_t ea;
} ap_m68030_single_t;

[[nodiscard]] ap_m68030_single_t ap_m68030_single_decode(uint16_t instruction);

/* Whether the instruction may only be executed in supervisor state. */
[[nodiscard]] bool ap_m68030_single_privileged(ap_m68030_single_kind_t kind);

#endif /* APOLLO_CPU_M68030_AP_M68030_SINGLE_H */
