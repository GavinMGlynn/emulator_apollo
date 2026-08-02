/* MC68851 coprocessor interface registers and response primitives. See the
 * header for why this is not the 68882's CIR table with two flags changed. */

#include "cpu/m68851/ap_m68851_cir.h"

ap_m68851_cir_t ap_m68851_cir_decode(unsigned select) {
  select &= 0x1Fu;

  /* Table 9-2's wide selects first: `100xx` and above have two or three
   * don't-care bits, because a 32-bit register is reachable at four byte
   * offsets. */
  if ((select & 0x1Cu) == 0x10u) {
    return AP_M68851_CIR_OPERAND; /* 100xx */
  }
  if ((select & 0x18u) == 0x18u) {
    /* `110xx` and `111xx`. */
    return (select & 0x04u) ? AP_M68851_CIR_OPERAND_ADDRESS
                            : AP_M68851_CIR_INSTRUCTION_ADDRESS;
  }

  /* The rest are `nnnnx`: one don't-care bit, so a 16-bit register occupies two
   * byte addresses. */
  switch (select >> 1) {
  case 0x0u:
    return AP_M68851_CIR_RESPONSE;
  case 0x1u:
    return AP_M68851_CIR_CONTROL;
  case 0x2u:
    return AP_M68851_CIR_SAVE;
  case 0x3u:
    return AP_M68851_CIR_RESTORE;
  case 0x4u:
    return AP_M68851_CIR_OPERATION_WORD;
  case 0x5u:
    return AP_M68851_CIR_COMMAND;
  case 0x6u:
    return AP_M68851_CIR_RESERVED; /* $0C */
  case 0x7u:
    return AP_M68851_CIR_CONDITION;
  case 0xAu:
    return AP_M68851_CIR_REGISTER_SELECT;
  case 0xBu:
    return AP_M68851_CIR_RESERVED; /* $16 */
  default:
    return AP_M68851_CIR_RESERVED;
  }
}

bool ap_m68851_cir_implemented(ap_m68851_cir_t cir) {
  /* Table 9-2's asterisks: the operation word and instruction address CIRs. */
  return cir != AP_M68851_CIR_OPERATION_WORD &&
         cir != AP_M68851_CIR_INSTRUCTION_ADDRESS &&
         cir != AP_M68851_CIR_RESERVED;
}

bool ap_m68851_cir_write_can_violate(ap_m68851_cir_t cir) {
  /* The two unimplemented registers are explicitly exempt -- "will not cause a
   * protocol violation" appears in both descriptions. The operand address CIR,
   * which *is* implemented, is the one that faults: "any other write will cause
   * a protocol violation". */
  return cir == AP_M68851_CIR_OPERAND_ADDRESS;
}

unsigned ap_m68851_cir_width(ap_m68851_cir_t cir) {
  switch (cir) {
  case AP_M68851_CIR_OPERAND:
  case AP_M68851_CIR_INSTRUCTION_ADDRESS:
  case AP_M68851_CIR_OPERAND_ADDRESS:
    return 32u;
  case AP_M68851_CIR_RESERVED:
    return 0u;
  case AP_M68851_CIR_RESPONSE:
  case AP_M68851_CIR_CONTROL:
  case AP_M68851_CIR_SAVE:
  case AP_M68851_CIR_RESTORE:
  case AP_M68851_CIR_OPERATION_WORD:
  case AP_M68851_CIR_COMMAND:
  case AP_M68851_CIR_CONDITION:
  case AP_M68851_CIR_REGISTER_SELECT:
    return 16u;
  }
  return 0u;
}

bool ap_m68851_cir_readable(ap_m68851_cir_t cir) {
  switch (cir) {
  case AP_M68851_CIR_RESPONSE:
  case AP_M68851_CIR_SAVE:
  case AP_M68851_CIR_RESTORE:
  case AP_M68851_CIR_OPERAND:
  case AP_M68851_CIR_REGISTER_SELECT:
    return true;
  case AP_M68851_CIR_CONTROL:
  case AP_M68851_CIR_COMMAND:
  case AP_M68851_CIR_CONDITION:
  case AP_M68851_CIR_OPERATION_WORD:
  case AP_M68851_CIR_INSTRUCTION_ADDRESS:
  /* "Reads from this register are ignored and always return all ones" --
   * so the operand address CIR is write-only in practice, which is what
   * Table 9-2's `Write` column records. */
  case AP_M68851_CIR_OPERAND_ADDRESS:
  case AP_M68851_CIR_RESERVED:
    return false;
  }
  return false;
}

bool ap_m68851_cir_writable(ap_m68851_cir_t cir) {
  switch (cir) {
  case AP_M68851_CIR_CONTROL:
  case AP_M68851_CIR_RESTORE:
  case AP_M68851_CIR_COMMAND:
  case AP_M68851_CIR_CONDITION:
  case AP_M68851_CIR_OPERAND:
  case AP_M68851_CIR_OPERAND_ADDRESS:
    return true;
  case AP_M68851_CIR_RESPONSE:
  case AP_M68851_CIR_SAVE:
  case AP_M68851_CIR_REGISTER_SELECT:
  case AP_M68851_CIR_OPERATION_WORD:
  case AP_M68851_CIR_INSTRUCTION_ADDRESS:
  case AP_M68851_CIR_RESERVED:
    return false;
  }
  return false;
}

ap_m68851_null_usage_t
ap_m68851_null_classify(ap_m68851_null_primitive_t primitive) {
  /* "Primitives returned by the MC68851 do not have the PC bit set", and none
   * of Table 9-3's rows sets IA. */
  if (primitive.pass_pc || primitive.interrupts_allowed) {
    return AP_M68851_NULL_NOT_USED_BY_THIS_PART;
  }

  if (primitive.come_again) {
    /* CA=1 PC=0 IA=0 PF=0 TF=0. */
    if (primitive.processing_finished || primitive.true_false) {
      return AP_M68851_NULL_NOT_USED_BY_THIS_PART;
    }
    return AP_M68851_NULL_COME_AGAIN;
  }

  if (!primitive.processing_finished) {
    /* CA=0 with PF=0 is none of the three. */
    return AP_M68851_NULL_NOT_USED_BY_THIS_PART;
  }

  /* CA=0 PF=1. With TF clear this is the idle encoding, which the condition
   * result also uses for a false condition -- the two are told apart by
   * context and not by bits, so a false condition reports as idle here. */
  return primitive.true_false ? AP_M68851_NULL_CONDITION_RESULT
                              : AP_M68851_NULL_IDLE;
}

bool ap_m68851_vector_timing(unsigned vector,
                             ap_m68851_exception_timing_t *timing) {
  switch (vector) {
  case AP_M68851_VECTOR_F_LINE:
  case AP_M68851_VECTOR_PROTOCOL_VIOLATION:
    *timing = AP_M68851_EXCEPTION_PRE_INSTRUCTION;
    return true;
  case AP_M68851_VECTOR_CONFIGURATION_ERROR:
  case AP_M68851_VECTOR_ILLEGAL_OPERATION:
  case AP_M68851_VECTOR_ACCESS_VIOLATION:
    *timing = AP_M68851_EXCEPTION_POST_INSTRUCTION;
    return true;
  default:
    return false;
  }
}

uint32_t ap_m68851_vector_offset(unsigned vector) {
  return (uint32_t)vector * 4u;
}
