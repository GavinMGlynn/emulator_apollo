/* MC68040 floating-point unit: what it implements, and what it refuses.
 *
 * `MC68040 User's Manual (1993)` §9.6.1 and Table 9-10.
 *
 * ## The interesting part of this FPU is its subset
 *
 * The 68882 executes forty-odd operations in silicon. The 68040 executes a
 * *subset* and traps the rest to the `M68040FPSP`, Motorola's floating-point
 * software package, and Table 9-10 says exactly which:
 *
 *   monadic  FACOS FASIN FATAN FATANH FCOS FCOSH FETOX FETOXM1 FGETEXP
 *            FGETMAN FINT FINTRZ FLOG10 FLOGN FLOGNP1 FMOVECR FSIN FSINCOS
 *            FSINH FTAN FTANH FTENTOX FTWOTOX
 *   dyadic   FMOD FREM FSCALE
 *
 * That list is worth reading twice. Every transcendental is on it, which is
 * unsurprising -- but so are `FINT`, `FINTRZ`, `FGETEXP`, `FGETMAN`, `FSCALE`,
 * `FMOD` and `FREM`, which are *exactly specified* and which this core already
 * implements bit-exactly for the 68882. Motorola moved them to software anyway.
 *
 * ## Table 9-10 omits `FLOG2`, and the same manual proves it
 *
 * The table lists `FLOG10`, `FLOGN` and `FLOGNP1` and not `FLOG2` -- confirmed
 * in the page image, so it is not an extraction artefact. That would mean the
 * part computes log base 2 in silicon while trapping log base 10 and natural
 * log, which makes no engineering sense: the other two are log base 2 times a
 * constant, so hardware that had log2 would get them nearly free.
 *
 * Appendix E settles it without leaving the manual. Table E-2, "instructions
 * the M68040FPSP provides", lists `FLOG2` among the transcendentals alongside
 * `FLOG10`, `FLOGN` and `FLOGNP1`, and without the asterisk that marks the
 * instructions the hardware *does* implement except for special data types. An
 * instruction the software package emulates outright is one the hardware does
 * not have, so `FLOG2` is unimplemented and Table 9-10 is defective.
 *
 * This is the resolution order paying off inside a single document: the
 * sibling section answered what the obvious table got wrong.
 *
 * ## What that means for this core
 *
 * The 68882's transcendentals are a documented divergence: not implemented,
 * because Motorola publishes bounds rather than an algorithm and a
 * correctly-rounded implementation would be *more* accurate than the part.
 * On the 68040 there is no such tension. Refusing these instructions **is** the
 * hardware's behaviour, so a model that computed them would be wrong in a way
 * that no amount of accuracy could fix: it would skip an exception the
 * operating system is required to handle, and Domain/OS on a DN5500 supplies
 * that handler.
 *
 * So the same instruction is a gap on one part and a feature on the other, and
 * this module exists to keep the two from being confused.
 *
 * ## Three outcomes, two of which share a vector
 *
 * "The MC68040 recognizes some F-line instructions ... which do not cause
 * F-line exceptions. There are some F-line instructions that the MC68040
 * recognizes as valid MC68881/MC68882 floating-point instruction patterns, but
 * as floating-point instructions that the processor cannot complete in
 * hardware ... If the processor encounters an F-line instruction and the
 * instruction patterns do not match either of the above two cases, the
 * processor takes an F-line illegal exception."
 *
 * And the sting: "Since the unimplemented floating-point exception and the
 * F-line illegal instruction **share the same vector**, the exception handler
 * uses the stack frame format ($0 or $2) to distinguish between the two."
 *
 * So the vector alone cannot tell an operating system which happened -- the
 * frame format is the only discriminator, and a model that pushed the wrong
 * one would send a legal `FSIN` to the illegal-instruction handler.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_FPU_H
#define APOLLO_CPU_M68040_AP_M68040_FPU_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68882/ap_m68882_decode.h"

typedef enum {
  /* Executed in hardware. */
  AP_M68040_FPU_IMPLEMENTED,
  /* "Valid MC68881/MC68882 floating-point instruction patterns, but ...
   * instructions that the processor cannot complete in hardware." Vector 11,
   * stack frame format $2. */
  AP_M68040_FPU_UNIMPLEMENTED_INSTRUCTION,
  /* Not a floating-point pattern at all. Vector 11, stack frame format $0. */
  AP_M68040_FPU_F_LINE_ILLEGAL,
} ap_m68040_fpu_outcome_t;

/* Both outcomes above take this vector; only the frame format differs. */
#define AP_M68040_FPU_VECTOR 11u
#define AP_M68040_FPU_FRAME_F_LINE_ILLEGAL 0u
#define AP_M68040_FPU_FRAME_UNIMPLEMENTED 2u

/* Whether Table 9-10 lists this operation. Takes the 68882's operation enum
 * because the 68040 "recognizes ... valid MC68881/MC68882 floating-point
 * instruction patterns" -- the encodings are the same part's, which is why the
 * decode is shared rather than duplicated. */
[[nodiscard]] bool ap_m68040_fpu_is_unimplemented(ap_m68882_operation_t op);

/* Classify a general-type floating-point instruction from its command word. */
[[nodiscard]] ap_m68040_fpu_outcome_t
ap_m68040_fpu_classify(uint16_t command_word);

/* The stack frame format an outcome pushes. Zero for the implemented case,
 * which pushes no frame at all. */
[[nodiscard]] unsigned
ap_m68040_fpu_frame_format(ap_m68040_fpu_outcome_t outcome);

#endif /* APOLLO_CPU_M68040_AP_M68040_FPU_H */
