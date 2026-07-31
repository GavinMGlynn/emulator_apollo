/* MC68030 instruction decode: the dispatcher over the family decoders.
 *
 * Each family of `M68000 Family Programmer's Reference Manual 1992` Table 8-2
 * has its own decoder in this directory. This is what turns them into a decoder
 * of *instructions*: given any 16-bit word, which family claims it and what did
 * that family make of it.
 *
 * ## Family 0100 needs three decoders, and their order matters
 *
 * "Miscellaneous" is really three disjoint subtrees, and they are tried in the
 * order their encodings actually nest: the `$4E` control group is a fixed top
 * byte and is recognised first; then the LEA/CHK and `$48`/`$4C` forms, which
 * all carry bit 8 set; then the single-operand group, which requires bit 8
 * clear. The last two cannot collide, and the ordering is stated here rather
 * than discovered by whoever adds the next one.
 *
 * ## What "illegal" means here
 *
 * A word no family claims is reported as illegal rather than silently assigned
 * to one. That matters because the alternative -- letting a decoder's fallback
 * absorb an unassigned encoding -- produces an instruction that executes, which
 * is the failure mode every one of these family modules was written to avoid.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_DECODE_H
#define APOLLO_CPU_M68030_AP_M68030_DECODE_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_arith.h"
#include "cpu/m68030/ap_m68030_branch.h"
#include "cpu/m68030/ap_m68030_control.h"
#include "cpu/m68030/ap_m68030_coproc.h"
#include "cpu/m68030/ap_m68030_immediate.h"
#include "cpu/m68030/ap_m68030_misc.h"
#include "cpu/m68030/ap_m68030_move.h"
#include "cpu/m68030/ap_m68030_quick.h"
#include "cpu/m68030/ap_m68030_shift.h"
#include "cpu/m68030/ap_m68030_single.h"

typedef enum {
  AP_M68030_DECODED_IMMEDIATE, /* family 0000 */
  AP_M68030_DECODED_MOVE,      /* 0001, 0010, 0011 */
  AP_M68030_DECODED_CONTROL,   /* 0100, the $4E group */
  AP_M68030_DECODED_MISC,      /* 0100, LEA/CHK and $48/$4C */
  AP_M68030_DECODED_SINGLE,    /* 0100, the single-operand group */
  AP_M68030_DECODED_QUICK,     /* 0101 */
  AP_M68030_DECODED_BRANCH,    /* 0110 */
  AP_M68030_DECODED_MOVEQ,     /* 0111 */
  AP_M68030_DECODED_ARITH,     /* 1000, 1001, 1011, 1100, 1101 */
  AP_M68030_DECODED_LINE_A,    /* 1010: unassigned, an emulator trap */
  AP_M68030_DECODED_SHIFT,     /* 1110 */
  AP_M68030_DECODED_COPROC,    /* 1111 */
  AP_M68030_DECODED_ILLEGAL,   /* no family claims it */
} ap_m68030_decoded_kind_t;

/* MOVEQ, family 0111: "0 1 1 1 REGISTER 0 DATA", with bit 8 clear and an
 * eight-bit signed immediate that is sign-extended to a long. */
typedef struct {
  unsigned reg;
  int8_t data;
} ap_m68030_moveq_t;

typedef struct {
  ap_m68030_decoded_kind_t kind;
  union {
    ap_m68030_immediate_t immediate;
    ap_m68030_move_t move;
    ap_m68030_control_t control;
    ap_m68030_misc_t misc;
    ap_m68030_single_t single;
    ap_m68030_quick_t quick;
    ap_m68030_branch_t branch;
    ap_m68030_moveq_t moveq;
    ap_m68030_arith_t arith;
    ap_m68030_shift_t shift;
    ap_m68030_coproc_t coproc;
  } as;
} ap_m68030_decoded_t;

[[nodiscard]] ap_m68030_decoded_t ap_m68030_decode(uint16_t instruction);

/* Total instruction length in bytes, including every extension word.
 *
 * ## MOVE needs two extension words, and the second is at a variable offset
 *
 * MOVE is the only instruction with *two* effective addresses, and the source's
 * extension words come first. So the destination's extension word is not at a
 * fixed position -- it sits after however many words the source took, which the
 * caller cannot know until it has sized the source. That is why this takes two
 * extension words rather than one, and why `second_extension` is documented as
 * "the word following the source's extensions" rather than "the second word of
 * the instruction". For every other instruction it is ignored.
 *
 * `first_extension` is the word immediately after the instruction word, read
 * only when an effective address uses an indexed mode.
 *
 * Returns 0 when the length cannot be determined: an illegal instruction, or a
 * coprocessor instruction, whose format varies by coprocessor and is not
 * modelled here. Zero is never a real instruction length, so it cannot be
 * mistaken for one. */
[[nodiscard]] unsigned ap_m68030_instruction_length(
    const ap_m68030_decoded_t *decoded, uint16_t first_extension,
    uint16_t second_extension);

#endif /* APOLLO_CPU_M68030_AP_M68030_DECODE_H */
