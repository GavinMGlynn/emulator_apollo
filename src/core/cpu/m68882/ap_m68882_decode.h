/* MC68882 instruction decode: the operation word's type field and the command
 * word's opclass and extension.
 *
 * `MC68881/MC68882 User's Manual` §4.7, Tables 4-11 and 4-13.
 *
 * ## Two words, and the second is the instruction
 *
 * "All FPCP instructions begin with an operation word" carrying the coprocessor
 * ID and a three-bit *type*; for the general type the word after it is the
 * command word, whose extension field says which of forty-odd operations this
 * is. So the operation word alone cannot tell `FADD` from `FSIN` -- the 68030's
 * F-line decoder gets as far as "a coprocessor instruction for cpID 1" and
 * everything past that is here.
 *
 * ## The reserved encodings are not all illegal
 *
 * Table 4-13's footnote 3 is the trap in this module:
 *
 *   "Some extension field encodings are unspecified, are redundant with valid
 *   instructions implemented by the FPCP, and **do not cause an F-line
 *   exception if executed**. However, these encodings are reserved for future
 *   definition by Motorola ... The redundant encodings are: $05, $07, $0B, $13,
 *   $17, $1B, $29-$2F, $39, and $3B-$3F."
 *
 * So there are three classes and not two: defined, *redundant* (executes as
 * something, must not trap), and undefined at `$40-$7F` (footnote 2: "the FPCP
 * issues the take pre-instruction exception primitive with a vector number of
 * 11 to instruct the MPU to take an F-line emulator trap"). A decoder with only
 * two classes traps on code the hardware runs.
 */

#ifndef APOLLO_CPU_M68882_AP_M68882_DECODE_H
#define APOLLO_CPU_M68882_AP_M68882_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/* §4.7's type field, bits 8-6 of the operation word. */
typedef enum {
  AP_M68882_TYPE_GENERAL = 0,   /* arithmetic, FMOVE, FMOVEM */
  AP_M68882_TYPE_CONDITIONAL,   /* FDBcc, FScc, FTRAPcc */
  AP_M68882_TYPE_BRANCH_WORD,   /* FBcc.W */
  AP_M68882_TYPE_BRANCH_LONG,   /* FBcc.L */
  AP_M68882_TYPE_SAVE,          /* FSAVE */
  AP_M68882_TYPE_RESTORE,       /* FRESTORE */
  AP_M68882_TYPE_RESERVED_6,
  AP_M68882_TYPE_RESERVED_7,
} ap_m68882_instruction_type_t;

/* Table 4-11's opclass, the command word's bits 15-13. */
typedef enum {
  AP_M68882_OPCLASS_REGISTER = 0,        /* FPm to FPn */
  AP_M68882_OPCLASS_RESERVED_1,          /* "Undefined, reserved" */
  AP_M68882_OPCLASS_MEMORY_TO_REGISTER,  /* or move constant, when RX is 111 */
  AP_M68882_OPCLASS_REGISTER_TO_MEMORY,
  AP_M68882_OPCLASS_MOVE_TO_CONTROL,
  AP_M68882_OPCLASS_MOVE_FROM_CONTROL,
  AP_M68882_OPCLASS_MOVEM_TO_REGISTERS,
  AP_M68882_OPCLASS_MOVEM_FROM_REGISTERS,
} ap_m68882_opclass_t;

/* What Table 4-13 makes of an extension field. */
typedef enum {
  AP_M68882_EXTENSION_DEFINED,
  /* Footnote 3's list: unspecified, redundant with a defined instruction, and
   * explicitly *not* an F-line exception. */
  AP_M68882_EXTENSION_REDUNDANT,
  /* `$40-$7F`, footnote 2: an F-line emulator trap, vector 11. */
  AP_M68882_EXTENSION_UNDEFINED,
} ap_m68882_extension_class_t;

/* The operations Table 4-13 names. Only the ones with an encoding: the table's
 * gaps are the redundant and undefined classes above. */
typedef enum {
  AP_M68882_OP_FMOVE_TO_FPN = 0x00,
  AP_M68882_OP_FINT = 0x01,
  AP_M68882_OP_FSINH = 0x02,
  AP_M68882_OP_FINTRZ = 0x03,
  AP_M68882_OP_FSQRT = 0x04,
  AP_M68882_OP_FLOGNP1 = 0x06,
  AP_M68882_OP_FETOXM1 = 0x08,
  AP_M68882_OP_FTANH = 0x09,
  AP_M68882_OP_FATAN = 0x0A,
  AP_M68882_OP_FASIN = 0x0C,
  AP_M68882_OP_FATANH = 0x0D,
  AP_M68882_OP_FSIN = 0x0E,
  AP_M68882_OP_FTAN = 0x0F,
  AP_M68882_OP_FETOX = 0x10,
  AP_M68882_OP_FTWOTOX = 0x11,
  AP_M68882_OP_FTENTOX = 0x12,
  AP_M68882_OP_FLOGN = 0x14,
  AP_M68882_OP_FLOG10 = 0x15,
  AP_M68882_OP_FLOG2 = 0x16,
  AP_M68882_OP_FABS = 0x18,
  AP_M68882_OP_FCOSH = 0x19,
  AP_M68882_OP_FNEG = 0x1A,
  AP_M68882_OP_FACOS = 0x1C,
  AP_M68882_OP_FCOS = 0x1D,
  AP_M68882_OP_FGETEXP = 0x1E,
  AP_M68882_OP_FGETMAN = 0x1F,
  AP_M68882_OP_FDIV = 0x20,
  AP_M68882_OP_FMOD = 0x21,
  AP_M68882_OP_FADD = 0x22,
  AP_M68882_OP_FMUL = 0x23,
  AP_M68882_OP_FSGLDIV = 0x24,
  AP_M68882_OP_FREM = 0x25,
  AP_M68882_OP_FSCALE = 0x26,
  AP_M68882_OP_FSGLMUL = 0x27,
  AP_M68882_OP_FSUB = 0x28,
  /* `$30-$37`, eight encodings for one instruction: the low three bits name the
   * second destination register, since FSINCOS produces two results. */
  AP_M68882_OP_FSINCOS = 0x30,
  AP_M68882_OP_FCMP = 0x38,
  AP_M68882_OP_FTST = 0x3A,
} ap_m68882_operation_t;

typedef struct {
  bool is_coprocessor;  /* bits 15-12 are 1111 */
  unsigned cpid;        /* bits 11-9 */
  ap_m68882_instruction_type_t type; /* bits 8-6 */
  unsigned effective_address;        /* bits 5-0, type dependent */
} ap_m68882_operation_word_t;

typedef struct {
  ap_m68882_opclass_t opclass;
  unsigned rx; /* source register, source format, or FPcr select */
  unsigned ry; /* destination register */
  unsigned extension; /* seven bits */
  ap_m68882_extension_class_t extension_class;
  ap_m68882_operation_t operation; /* meaningful when DEFINED */
} ap_m68882_command_word_t;

[[nodiscard]] ap_m68882_operation_word_t
ap_m68882_decode_operation(uint16_t word);

[[nodiscard]] ap_m68882_command_word_t
ap_m68882_decode_command(uint16_t word);

/* Whether the opclass takes an external operand, which is what decides whether
 * the operation word's effective address field is an address at all: "if the
 * command word indicates that an operand external to the FPCP is to be fetched
 * or stored, the effective address field of the operation word is an MPU
 * effective address descriptor". For a register-to-register operation it is
 * not, and reading it as one would evaluate an address the instruction never
 * names. */
[[nodiscard]] bool ap_m68882_command_uses_memory(
    const ap_m68882_command_word_t *command);

#endif /* APOLLO_CPU_M68882_AP_M68882_DECODE_H */
