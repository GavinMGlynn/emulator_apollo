/* MC68030 exception processing: vectors, priority and stack frames.
 *
 * `[030]` §8, cited throughout.
 *
 * This module is the *shape* of exception processing — which vector, which
 * frame, and which of several simultaneous exceptions goes first. Taking an
 * exception (stacking, vector fetch, PC load) needs an instruction unit and a
 * memory system, and belongs with them; what is here is the part that is pure
 * fact from Table 8-1, Table 8-5 and Table 8-6, and that everything else will
 * be checked against.
 *
 * ## Priority is not execution order, and the manual says so twice
 *
 * "0.0 is the highest priority, 4.2 is the lowest", and then: "As a general
 * rule, the lower the priority of an exception, the sooner the handler routine
 * for that exception executes." Those are not in conflict — the higher-priority
 * exception is *processed* first, which stacks it deeper, so the lower-priority
 * handler is the one that actually runs first and returns into it. Reset is the
 * documented exception to its own rule: "its handler is executed first even
 * though it has the highest priority because the reset operation clears all
 * other exceptions."
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_EXCEPTION_H
#define APOLLO_CPU_M68030_AP_M68030_EXCEPTION_H

#include <stdbool.h>
#include <stdint.h>

/* Vector numbers, `[030]` Table 8-1. Only the assigned ones are named; the
 * reserved ranges are deliberately absent rather than given invented names. */
typedef enum {
  AP_M68030_VECTOR_RESET_SP = 0,
  AP_M68030_VECTOR_RESET_PC = 1,
  AP_M68030_VECTOR_BUS_ERROR = 2,
  AP_M68030_VECTOR_ADDRESS_ERROR = 3,
  AP_M68030_VECTOR_ILLEGAL_INSTRUCTION = 4,
  AP_M68030_VECTOR_ZERO_DIVIDE = 5,
  AP_M68030_VECTOR_CHK = 6,
  AP_M68030_VECTOR_TRAPCC = 7,
  AP_M68030_VECTOR_PRIVILEGE_VIOLATION = 8,
  AP_M68030_VECTOR_TRACE = 9,
  AP_M68030_VECTOR_LINE_A = 10,
  AP_M68030_VECTOR_LINE_F = 11,
  AP_M68030_VECTOR_COPROCESSOR_PROTOCOL = 13,
  AP_M68030_VECTOR_FORMAT_ERROR = 14,
  AP_M68030_VECTOR_UNINITIALISED_INTERRUPT = 15,
  AP_M68030_VECTOR_SPURIOUS_INTERRUPT = 24,
  AP_M68030_VECTOR_AUTOVECTOR_BASE = 24, /* level N autovector is base + N */
  AP_M68030_VECTOR_TRAP_BASE = 32,       /* TRAP #N is base + N, N = 0..15 */
  /* Table 8-1, sheet 2, p. 8-3. Named individually rather than derived from the
   * base, because the order is neither the FPSR exception byte's bit order nor
   * the trap priority order of `[FPCP]` §6.1.9 -- three orderings over the same
   * seven conditions, and `48 + anything` is wrong for all of them. */
  AP_M68030_VECTOR_FPCP_BASE = 48,
  AP_M68030_VECTOR_FPCP_BSUN = 48,    /* branch or set on unordered condition */
  AP_M68030_VECTOR_FPCP_INEXACT = 49, /* INEX1 and INEX2 share this one */
  AP_M68030_VECTOR_FPCP_DZ = 50,
  AP_M68030_VECTOR_FPCP_UNFL = 51,
  AP_M68030_VECTOR_FPCP_OPERR = 52,
  AP_M68030_VECTOR_FPCP_OVFL = 53,
  AP_M68030_VECTOR_FPCP_SNAN = 54,
  AP_M68030_VECTOR_MMU_CONFIGURATION = 56,
  AP_M68030_VECTOR_USER_BASE = 64,
} ap_m68030_vector_t;

/* "The displacement of an exception vector is added to the value in this
 * register to access the vector table" -- each vector is one long word, so the
 * offset is the number times four. Table 8-1 lists them, and the table is a
 * check on this rather than a thing to transcribe: vector 2 is $008, 24 is
 * $060, 32 is $080, 255 is $3FC. */
[[nodiscard]] uint32_t ap_m68030_vector_offset(unsigned vector);

/* "Level 1 Interrupt Autovector" is vector 25, through "Level 7" at 31. */
[[nodiscard]] unsigned ap_m68030_autovector(unsigned level);

/* TRAP #N, N = 0..15, occupies vectors 32-47. */
[[nodiscard]] unsigned ap_m68030_trap_vector(unsigned trap);

/* Exception priority, `[030]` Table 8-5. Encoded as group and relative
 * priority within it, e.g. address error is 1.0 and bus error 1.1, so a
 * comparison orders them correctly without a table lookup per pair. */
typedef struct {
  uint8_t group;    /* 0..4 */
  uint8_t relative; /* the digit after the point */
} ap_m68030_priority_t;

[[nodiscard]] ap_m68030_priority_t ap_m68030_exception_priority(unsigned vector);

/* True when `a` is processed before `b`. "0.0 is the highest priority, 4.2 is
 * the lowest", so a numerically smaller group/relative wins. */
[[nodiscard]] bool ap_m68030_priority_precedes(ap_m68030_priority_t a,
                                               ap_m68030_priority_t b);

/* Stack frame formats, `[030]` Table 8-6. The format code is the top nibble of
 * the word at +$06, above the vector offset. */
typedef enum {
  AP_M68030_FRAME_SHORT = 0x0,           /* four word */
  AP_M68030_FRAME_THROWAWAY = 0x1,       /* four word, thrown away by RTE */
  AP_M68030_FRAME_SIX_WORD = 0x2,        /* adds the instruction address */
  AP_M68030_FRAME_COPROCESSOR_MID = 0x9, /* 10 words */
  AP_M68030_FRAME_SHORT_BUS_FAULT = 0xA, /* 16 words */
  AP_M68030_FRAME_LONG_BUS_FAULT = 0xB,  /* 46 words */
} ap_m68030_frame_format_t;

/* Size in 16-bit words, from the names Table 8-6 gives each frame. */
[[nodiscard]] unsigned ap_m68030_frame_words(ap_m68030_frame_format_t format);

/* The word stacked at +$06: the format in 15-12 over the vector offset.
 *
 * Note it is the vector *offset*, not the vector number -- so TRAP #0 stacks
 * $2080 and not $2020. Getting this wrong produces a frame that RTE accepts and
 * that returns to the wrong handler. */
[[nodiscard]] uint16_t ap_m68030_frame_format_word(
    ap_m68030_frame_format_t format, unsigned vector);

/* Recover the two fields, as RTE does: "it examines the stack frame on top of
 * the active supervisor stack to determine if it is a valid frame". */
[[nodiscard]] ap_m68030_frame_format_t
ap_m68030_frame_format_of(uint16_t format_word);
[[nodiscard]] uint32_t ap_m68030_frame_vector_offset_of(uint16_t format_word);

/* Whether the MC68030 defines this format at all. RTE "determines if it is a
 * valid frame"; an undefined format is a format error, vector 14. */
[[nodiscard]] bool ap_m68030_frame_format_defined(uint16_t format_word);

/* Which frame Table 8-6 gives this exception. The table lists the exception
 * *types* against each frame, so this is a transcription of that column:
 *
 *   Format $0, four word   interrupt, format error, TRAP #N, illegal
 *                          instruction, A-line, F-line, privilege violation
 *   Format $2, six word    CHK, CHK2, cpTRAPcc, TRAPcc, TRAPV, trace, zero
 *                          divide, MMU configuration, coprocessor
 *                          post-instruction
 *   Format $9, 10 words    coprocessor mid-instruction, protocol violation
 *   Format $A/$B           address error and bus error
 *
 * The six-word frame's extra long word is the INSTRUCTION ADDRESS, "the address
 * of the instruction that caused the exception" -- distinct from the stacked
 * PC, which for every one of those points at the *next* instruction. An
 * exception given the four-word frame when it wants six leaves RTE reading the
 * vector offset out of the instruction address, so this is not a size the
 * caller may round up.
 *
 * Reset is not a frame: "For all exceptions other than reset, the third step is
 * to save the current processor context." Asking for vector 0 or 1 is a caller
 * error, and this returns the four-word format for want of anything truer --
 * the taker declines reset outright rather than relying on it. */
[[nodiscard]] ap_m68030_frame_format_t
ap_m68030_frame_for_vector(unsigned vector);

/* Whether the frame's PC field holds the address of the *next* instruction, or
 * of the instruction that caused the exception.
 *
 * Table 8-6 states this per exception in the bracketed column, and it is not a
 * property of the frame size: the four-word frame holds the next instruction
 * for an interrupt or a TRAP and the faulting one for illegal instruction,
 * A-line, F-line and privilege violation ("First word of instruction causing
 * Privilege Violation"), and format error stacks "[RTE or cpRESTORE
 * instruction]" -- the RTE that found the bad frame, not what follows it.
 * Everything in the six-word frame's row is "[Next instruction for all these
 * exceptions]".
 *
 * Defaulting to "next" is the mistake this exists to prevent: a privilege
 * violation handler that emulates the instruction and returns would skip it,
 * and a format error handler would return past the RTE it was meant to
 * diagnose. Both run; neither faults. */
[[nodiscard]] bool ap_m68030_stacks_next_instruction(unsigned vector);

/* Table 8-1's vector for an `AP_M68882_EXC_*` exception bit, or 0 for a bit
 * that has no trap of its own.
 *
 * The mapping lives on this side because the vector table is the MPU's, and it
 * is a written-out list because the numbering follows neither the FPSR bit
 * order nor `[FPCP]` §6.1.9's priority order -- three orderings over the same
 * seven conditions. `INEX1` and `INEX2` both give 49: §6.1.10, "INEX1 and
 * INEX2 share one exception vector".
 *
 * Takes the bare bit rather than the FPU's registers so this header keeps no
 * dependency on the coprocessor's types; `ap_m68882_trap_exception` decides
 * *which* bit, and this decides where it goes. */
[[nodiscard]] unsigned ap_m68030_fpu_trap_vector(unsigned exception_bit);

/* Whether an interrupt request at `level` is recognised against a status
 * register interrupt mask of `mask`.
 *
 * Levels 1-6 are level sensitive and masked: recognised when the request
 * "exceeds the current interrupt priority mask in the status register".
 *
 * Level 7 is not. "Priority level 7, the nonmaskable interrupt (NMI), is a
 * special case. Level 7 interrupts cannot be masked by the interrupt priority
 * mask, and they are transition sensitive. The processor recognizes an
 * interrupt request each time the external interrupt request level changes from
 * some lower level to level 7, regardless of the value in the mask." So a level
 * 7 request needs to know the *previous* level to decide, which a pure
 * comparison against the mask cannot express -- hence `previous_level`. */
[[nodiscard]] bool ap_m68030_interrupt_recognised(unsigned level,
                                                  unsigned previous_level,
                                                  unsigned mask);

#endif /* APOLLO_CPU_M68030_AP_M68030_EXCEPTION_H */
