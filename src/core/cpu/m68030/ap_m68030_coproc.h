/* MC68030 family 1111: the coprocessor interface, and the MMU instructions.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §8.2 for the encoding, and
 * `[030]` §9.7.6 p. 9-64 for what the 68030 does with it. The last family of
 * the operation code map.
 *
 * ## cpID 0 is the 68030's own MMU
 *
 * "The MMU instructions use the same opcodes and coprocessor identification
 * (CpID) as the corresponding instructions of the MC68851." So `PMOVE`,
 * `PTEST` and `PFLUSH` are F-line instructions with coprocessor ID zero, and on
 * this machine that matters twice over: it is how the MMU registers this
 * project already models are reached, and the 68882 sits at a *different* ID
 * alongside it.
 *
 * ## The same word takes different vectors depending on privilege
 *
 * This is the unusual part, and it is stated plainly:
 *
 *   "All F-line instructions with CpID = 0 ... that the MC68030 does not
 *   support automatically cause F-line unimplemented instruction exceptions
 *   when their execution is attempted in the supervisor mode. If execution of
 *   an unimplemented F-line instruction with CpID = 0 is attempted in the user
 *   mode, the MC68030 takes a privilege violation exception."
 *
 * So one instruction word yields vector 11 from supervisor state and vector 8
 * from user state. Almost everywhere else in this architecture the exception a
 * word takes is a property of the word; here it is a property of the word *and*
 * the mode. Reporting F-line in both cases would let a user program distinguish
 * "unimplemented" from "not allowed", which is precisely what the privilege
 * violation exists to prevent.
 *
 * "F-line instructions with a CpID other than zero are executed as coprocessor
 * instructions by the MC68030" -- no privilege rule attaches to those.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_COPROC_H
#define APOLLO_CPU_M68030_AP_M68030_COPROC_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"

/* Bits 8-6, the coprocessor operation type. */
typedef enum {
  AP_M68030_CP_GENERAL = 0x0,   /* cpGEN: the arithmetic and move forms */
  AP_M68030_CP_CONDITIONAL = 0x1, /* cpScc, cpDBcc, cpTRAPcc */
  AP_M68030_CP_BRANCH_WORD = 0x2, /* cpBcc with a word displacement */
  AP_M68030_CP_BRANCH_LONG = 0x3, /* cpBcc with a long displacement */
  AP_M68030_CP_SAVE = 0x4,
  AP_M68030_CP_RESTORE = 0x5,
  AP_M68030_CP_RESERVED = 0x6,
} ap_m68030_coproc_type_t;

/* The MMU is coprocessor zero on this part. */
#define AP_M68030_CPID_MMU 0u

typedef struct {
  bool valid;                   /* the word is in family 1111 at all */
  unsigned cpid;                /* bits 11-9 */
  ap_m68030_coproc_type_t type; /* bits 8-6 */
  bool is_mmu;                  /* cpID 0: the 68030's own MMU instructions */
  /* The MMU instructions put a six-bit effective address in the low bits of the
   * instruction word, where a general coprocessor instruction has one too. Only
   * "control alterable addressing modes" are legal there on this part, which is
   * a restriction the executor applies -- this reports what the field says. */
  ap_m68030_ea_t ea;
} ap_m68030_coproc_t;

[[nodiscard]] ap_m68030_coproc_t ap_m68030_coproc_decode(uint16_t instruction);

/* The vector an *unsupported* F-line instruction takes, which for coprocessor
 * zero depends on the privilege state it was attempted from. Returns the
 * exception vector number. */
[[nodiscard]] unsigned ap_m68030_coproc_unsupported_vector(
    const ap_m68030_coproc_t *coproc, bool supervisor);

#endif /* APOLLO_CPU_M68030_AP_M68030_COPROC_H */
