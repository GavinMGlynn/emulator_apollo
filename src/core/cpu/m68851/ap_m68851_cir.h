/* MC68851 coprocessor interface registers and response primitives.
 *
 * `MC68851 PMMU User's Manual, Third Edition` §9.1.2 (Table 9-2), §9.2.2
 * (Table 9-3) and Table 9-6.
 *
 * ## Two coprocessors, one interface, complementary subsets
 *
 * The 68851 and the 68882 sit on the same M68000 coprocessor interface at cpID
 * 0 and 1, and Table 9-2's footnote marks which registers each part leaves
 * unimplemented. They are *not* the same two:
 *
 *                          68851            68882
 *     $08 Operation Word   unimplemented    unimplemented
 *     $18 Instruction Addr unimplemented    implemented
 *     $1C Operand Address  implemented      unimplemented
 *
 * And the reason is concurrency. The instruction address CIR "is used to
 * support concurrent processor/coprocessor instruction execution and is not
 * implemented by the MC68851. Primitives returned by the MC68851 do not have
 * the PC bit set" -- the MMU never runs concurrently with the CPU, so it never
 * needs to report which instruction an exception belongs to. The operand
 * address CIR, conversely, exists here because `PFLUSH`, `PLOAD`, `PTEST` and
 * `PVALID` all evaluate an effective address the MMU then uses, which is
 * something no floating-point instruction does.
 *
 * A model that shared one CIR table between the two parts would therefore be
 * wrong in both directions.
 *
 * ## Unimplemented is not the same as reserved
 *
 * The manual distinguishes three fates, and only one of them faults:
 *
 *  - `$08`, unimplemented: "the operation word CIR location should never be
 *    written by the main processor. If a write to this location occurs, it will
 *    be ignored and **will not cause a protocol violation**."
 *  - `$18`, unimplemented: "all writes to this CIR are ignored and reads return
 *    all ones. Accessing this register will not cause a protocol violation."
 *  - `$1C`, implemented but constrained: "writes to this CIR are legal only in
 *    response to the evaluate and transfer effective address primitive. Any
 *    other write **will cause a protocol violation**, the faulting cycle will
 *    be ignored, and the instruction currently being executed (if any) will be
 *    aborted."
 *
 * So the register that *is* implemented is the one that can fault, and the two
 * that are not are silently tolerant. That is the opposite of the intuitive
 * arrangement and it is what a protocol-violation test will turn on.
 */

#ifndef APOLLO_CPU_M68851_AP_M68851_CIR_H
#define APOLLO_CPU_M68851_AP_M68851_CIR_H

#include <stdbool.h>
#include <stdint.h>

/* Table 9-2's registers, by their offsets. */
typedef enum {
  AP_M68851_CIR_RESPONSE,            /* $00, 16-bit, read */
  AP_M68851_CIR_CONTROL,             /* $02, 16-bit, write */
  AP_M68851_CIR_SAVE,                /* $04, 16-bit, read */
  AP_M68851_CIR_RESTORE,             /* $06, 16-bit, read/write */
  AP_M68851_CIR_OPERATION_WORD,      /* $08, unimplemented on this part */
  AP_M68851_CIR_COMMAND,             /* $0A, 16-bit, write */
  AP_M68851_CIR_CONDITION,           /* $0E, 16-bit, write */
  AP_M68851_CIR_OPERAND,             /* $10, 32-bit, read/write */
  AP_M68851_CIR_REGISTER_SELECT,     /* $14, 16-bit, read */
  AP_M68851_CIR_INSTRUCTION_ADDRESS, /* $18, unimplemented on this part */
  AP_M68851_CIR_OPERAND_ADDRESS,     /* $1C, 32-bit, write */
  AP_M68851_CIR_RESERVED,            /* $0C and $16 */
} ap_m68851_cir_t;

/* Decode the CIR select field, address bits A4-A0. Table 9-2's low bits are
 * don't-cares -- `Operand` is `100xx` and `Operand Address` is `111xx`, four
 * addresses each, because a 32-bit register may be reached by byte or word
 * accesses at any offset within it. */
[[nodiscard]] ap_m68851_cir_t ap_m68851_cir_decode(unsigned select);

/* Whether this part implements the register at all. */
[[nodiscard]] bool ap_m68851_cir_implemented(ap_m68851_cir_t cir);

/* Whether a write to this register outside its protocol raises a coprocessor
 * protocol violation. Only `$1C` does among the unusual ones -- the two
 * unimplemented registers are explicitly exempt. */
[[nodiscard]] bool ap_m68851_cir_write_can_violate(ap_m68851_cir_t cir);

/* Register width in bits, 16 or 32; zero for the reserved locations. */
[[nodiscard]] unsigned ap_m68851_cir_width(ap_m68851_cir_t cir);

[[nodiscard]] bool ap_m68851_cir_readable(ap_m68851_cir_t cir);
[[nodiscard]] bool ap_m68851_cir_writable(ap_m68851_cir_t cir);

/* "Read accesses of a write-only register always return all ones." Applies to
 * the unimplemented registers too: "reads return all ones". */
#define AP_M68851_CIR_UNREADABLE_VALUE 0xFFFFFFFFu

/* ---------------------------------------------------------------------------
 * Response primitives, §9.2.2 and Table 9-3.
 * ------------------------------------------------------------------------- */

/* The null primitive's control bits. "There are 32 possible null primitive
 * encodings of which the MC68851 uses only three." */
typedef struct {
  bool come_again;     /* CA */
  bool pass_pc;        /* PC -- never set by this part */
  bool interrupts_allowed; /* IA */
  bool processing_finished; /* PF */
  bool true_false;     /* TF */
} ap_m68851_null_primitive_t;

/* Table 9-3's three rows. */
typedef enum {
  /* CA=0 PC=0 IA=0 PF=1 TF=0. "Returned when the MC68851 is in the idle state
   * or as the final primitive of an instruction dialog." */
  AP_M68851_NULL_IDLE,
  /* CA=1 PC=0 IA=0 PF=0 TF=0. "Requires further service from the main
   * processor ... the expected response is for the main processor to re-read
   * the response CIR." */
  AP_M68851_NULL_COME_AGAIN,
  /* CA=0 PC=0 IA=0 PF=1 TF=0/1. "Returned by the MC68851 in response to the
   * write of a conditional predicate to the condition CIR. The TF bit
   * indicates the result of the conditional evaluation." */
  AP_M68851_NULL_CONDITION_RESULT,
  AP_M68851_NULL_NOT_USED_BY_THIS_PART,
} ap_m68851_null_usage_t;

/* Classify a null primitive's bits against Table 9-3. `AP_M68851_NULL_IDLE`
 * and `AP_M68851_NULL_CONDITION_RESULT` share an encoding when `TF` is clear --
 * the manual distinguishes them by context, not by bits -- so this reports the
 * idle form for that pattern and the condition form only when `TF` is set. */
[[nodiscard]] ap_m68851_null_usage_t
ap_m68851_null_classify(ap_m68851_null_primitive_t primitive);

/* ---------------------------------------------------------------------------
 * Table 9-6's vector numbers.
 * ------------------------------------------------------------------------- */

/* Whether an exception is reported before or after the instruction. The split
 * matters because it decides which stack frame the CPU builds and where
 * execution resumes. */
typedef enum {
  AP_M68851_EXCEPTION_PRE_INSTRUCTION,
  AP_M68851_EXCEPTION_POST_INSTRUCTION,
} ap_m68851_exception_timing_t;

#define AP_M68851_VECTOR_F_LINE 11u              /* $02C, pre-instruction */
#define AP_M68851_VECTOR_PROTOCOL_VIOLATION 13u  /* $034, pre-instruction */
#define AP_M68851_VECTOR_CONFIGURATION_ERROR 56u /* $0E0, post-instruction */
#define AP_M68851_VECTOR_ILLEGAL_OPERATION 57u   /* $0E4, post-instruction */
#define AP_M68851_VECTOR_ACCESS_VIOLATION 58u    /* $0E8, post-instruction */

/* Whether a vector number is one this part raises, and when. Returns false for
 * any other vector. */
[[nodiscard]] bool
ap_m68851_vector_timing(unsigned vector, ap_m68851_exception_timing_t *timing);

/* The vector's offset in the exception table: four bytes per vector. Provided
 * so the transcribed offsets in Table 9-6 can be checked rather than trusted --
 * a table that disagreed with `vector * 4` would mean one column was misread. */
[[nodiscard]] uint32_t ap_m68851_vector_offset(unsigned vector);

#endif /* APOLLO_CPU_M68851_AP_M68851_CIR_H */
