/* MC68030 exception processing. See ap_m68030_exception.h for the citations and
 * for why priority order is not handler execution order. */

#include "cpu/m68030/ap_m68030_exception.h"

uint32_t ap_m68030_vector_offset(unsigned vector) {
  return (uint32_t)vector * 4u;
}

unsigned ap_m68030_autovector(unsigned level) {
  return AP_M68030_VECTOR_AUTOVECTOR_BASE + level;
}

unsigned ap_m68030_trap_vector(unsigned trap) {
  return AP_M68030_VECTOR_TRAP_BASE + trap;
}

ap_m68030_priority_t ap_m68030_exception_priority(unsigned vector) {
  switch (vector) {
  /* "0.0 - Reset. Aborts all processing (instruction or exception) and does not
   * save old context." */
  case AP_M68030_VECTOR_RESET_SP:
  case AP_M68030_VECTOR_RESET_PC:
    return (ap_m68030_priority_t){0, 0};

  /* Group 1 "Suspends processing ... and saves internal context", with address
   * error above bus error. */
  case AP_M68030_VECTOR_ADDRESS_ERROR:
    return (ap_m68030_priority_t){1, 0};
  case AP_M68030_VECTOR_BUS_ERROR:
    return (ap_m68030_priority_t){1, 1};

  /* Group 2, "Exception processing is part of instruction execution": BKPT,
   * CHK, CHK2, cp mid-instruction, cp protocol violation, cpTRAPcc, divide by
   * zero, RTE, TRAP #n, TRAPV, MMU configuration. */
  case AP_M68030_VECTOR_CHK:
  case AP_M68030_VECTOR_TRAPCC:
  case AP_M68030_VECTOR_ZERO_DIVIDE:
  case AP_M68030_VECTOR_COPROCESSOR_PROTOCOL:
  case AP_M68030_VECTOR_MMU_CONFIGURATION:
    return (ap_m68030_priority_t){2, 0};

  /* Group 3, "Exception processing begins before instruction is executed":
   * illegal instruction, line A, unimplemented line F, privilege violation,
   * cp pre-instruction. Format error is raised by RTE examining the frame, so
   * it belongs with group 2's "part of instruction execution" rather than
   * here -- but the manual does not list it in Table 8-5 at all, so it is
   * grouped with the instruction-execution exceptions it accompanies and that
   * choice is stated rather than hidden. */
  case AP_M68030_VECTOR_ILLEGAL_INSTRUCTION:
  case AP_M68030_VECTOR_LINE_A:
  case AP_M68030_VECTOR_LINE_F:
  case AP_M68030_VECTOR_PRIVILEGE_VIOLATION:
    return (ap_m68030_priority_t){3, 0};

  /* Group 4, "Exception processing begins when current instruction or previous
   * exception processing is completed": cp post-instruction 4.0, trace 4.1,
   * interrupt 4.2. */
  case AP_M68030_VECTOR_TRACE:
    return (ap_m68030_priority_t){4, 1};

  default:
    break;
  }

  /* TRAP #n is group 2 with the rest of the instruction-execution exceptions. */
  if (vector >= AP_M68030_VECTOR_TRAP_BASE && vector < AP_M68030_VECTOR_FPCP_BASE) {
    return (ap_m68030_priority_t){2, 0};
  }

  /* Interrupts -- the autovectors, the spurious vector and every user vector --
   * are 4.2, the lowest priority the table defines. */
  if (vector == AP_M68030_VECTOR_SPURIOUS_INTERRUPT ||
      (vector > AP_M68030_VECTOR_AUTOVECTOR_BASE &&
       vector <= AP_M68030_VECTOR_AUTOVECTOR_BASE + 7u) ||
      vector >= AP_M68030_VECTOR_USER_BASE) {
    return (ap_m68030_priority_t){4, 2};
  }

  /* Anything else the manual does not place: treated as group 2, the
   * instruction-execution group, which is where the unlisted assigned vectors
   * (format error, uninitialised interrupt) arise. */
  return (ap_m68030_priority_t){2, 0};
}

bool ap_m68030_priority_precedes(ap_m68030_priority_t a,
                                 ap_m68030_priority_t b) {
  if (a.group != b.group) {
    return a.group < b.group;
  }
  return a.relative < b.relative;
}

unsigned ap_m68030_frame_words(ap_m68030_frame_format_t format) {
  switch (format) {
  case AP_M68030_FRAME_SHORT:
  case AP_M68030_FRAME_THROWAWAY:
    return 4;
  case AP_M68030_FRAME_SIX_WORD:
    return 6;
  case AP_M68030_FRAME_COPROCESSOR_MID:
    return 10;
  case AP_M68030_FRAME_SHORT_BUS_FAULT:
    return 16;
  case AP_M68030_FRAME_LONG_BUS_FAULT:
    return 46;
  }
  return 0;
}

uint16_t ap_m68030_frame_format_word(ap_m68030_frame_format_t format,
                                     unsigned vector) {
  /* The offset, not the vector number: Table 8-6 labels the field "VECTOR
   * OFFSET", and §4.3.1 says the offset is what is added to the VBR. */
  const uint32_t offset = ap_m68030_vector_offset(vector);
  return (uint16_t)(((uint32_t)format << 12) | (offset & 0x0FFFu));
}

ap_m68030_frame_format_t ap_m68030_frame_format_of(uint16_t format_word) {
  return (ap_m68030_frame_format_t)((format_word >> 12) & 0xFu);
}

uint32_t ap_m68030_frame_vector_offset_of(uint16_t format_word) {
  return (uint32_t)(format_word & 0x0FFFu);
}

bool ap_m68030_frame_format_defined(uint16_t format_word) {
  switch (ap_m68030_frame_format_of(format_word)) {
  case AP_M68030_FRAME_SHORT:
  case AP_M68030_FRAME_THROWAWAY:
  case AP_M68030_FRAME_SIX_WORD:
  case AP_M68030_FRAME_COPROCESSOR_MID:
  case AP_M68030_FRAME_SHORT_BUS_FAULT:
  case AP_M68030_FRAME_LONG_BUS_FAULT:
    return true;
  }
  return false;
}

ap_m68030_frame_format_t ap_m68030_frame_for_vector(unsigned vector) {
  switch (vector) {
  /* "Address Error or Bus Error -- Execution Unit at Instruction Boundary" is
   * the short bus fault frame; the long one is the mid-instruction case, which
   * needs the internal state this model does not carry. The short form is
   * reported, and the taker declines both. */
  case AP_M68030_VECTOR_BUS_ERROR:
  case AP_M68030_VECTOR_ADDRESS_ERROR:
    return AP_M68030_FRAME_SHORT_BUS_FAULT;

  /* "Main-Detected Protocol Violation" sits in the coprocessor
   * mid-instruction frame, not in either of the normal two. */
  case AP_M68030_VECTOR_COPROCESSOR_PROTOCOL:
    return AP_M68030_FRAME_COPROCESSOR_MID;

  /* The six-word frame's list, each of which needs the instruction address
   * kept separately from the return PC. */
  case AP_M68030_VECTOR_ZERO_DIVIDE:
  case AP_M68030_VECTOR_CHK:     /* CHK and CHK2 share this vector */
  case AP_M68030_VECTOR_TRAPCC:  /* cpTRAPcc, TRAPcc and TRAPV share it */
  case AP_M68030_VECTOR_TRACE:
  case AP_M68030_VECTOR_MMU_CONFIGURATION:
    return AP_M68030_FRAME_SIX_WORD;

  default:
    break;
  }

  /* Everything else Table 8-6 names -- interrupts, format error, TRAP #N,
   * illegal instruction, the two emulator lines and privilege violation --
   * takes the four-word frame. */
  return AP_M68030_FRAME_SHORT;
}

bool ap_m68030_stacks_next_instruction(unsigned vector) {
  switch (vector) {
  /* Table 8-6's four-word row, the entries that name the faulting instruction
   * rather than the one after it. */
  case AP_M68030_VECTOR_ILLEGAL_INSTRUCTION:
  case AP_M68030_VECTOR_LINE_A:
  case AP_M68030_VECTOR_LINE_F:
  case AP_M68030_VECTOR_PRIVILEGE_VIOLATION:
  case AP_M68030_VECTOR_FORMAT_ERROR:
    return false;
  default:
    break;
  }
  /* Everything else -- the interrupts, TRAP #N, and the whole six-word row,
   * "[Next instruction for all these exceptions]". */
  return true;
}

bool ap_m68030_interrupt_recognised(unsigned level, unsigned previous_level,
                                    unsigned mask) {
  if (level == 0) {
    return false; /* "Indicates that no interrupt is requested." */
  }

  /* "Level 7 interrupts cannot be masked by the interrupt priority mask, and
   * they are transition sensitive. The processor recognizes an interrupt
   * request each time the external interrupt request level changes from some
   * lower level to level 7, regardless of the value in the mask."
   *
   * So a level 7 request already standing at level 7 is *not* a new interrupt,
   * which is exactly the case that distinguishes it from levels 1-6: holding
   * the line at 7 does not re-interrupt, but dropping it and raising it does. */
  if (level == 7u) {
    return previous_level < 7u;
  }

  /* Levels 1-6 are recognised when the request "exceeds the current interrupt
   * priority mask". */
  return level > mask;
}
