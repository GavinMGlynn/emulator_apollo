/* MC68882 instruction decode. See ap_m68882_decode.h for why the reserved
 * encodings fall into three classes rather than two. */

#include "cpu/m68882/ap_m68882_decode.h"

ap_m68882_operation_word_t ap_m68882_decode_operation(uint16_t word) {
  ap_m68882_operation_word_t out = {0};
  out.is_coprocessor = (word >> 12) == 0xFu;
  out.cpid = (unsigned)((word >> 9) & 7u);
  out.type = (ap_m68882_instruction_type_t)((word >> 6) & 7u);
  out.effective_address = (unsigned)(word & 0x3Fu);
  return out;
}

/* Table 4-13's defined encodings. A switch rather than a table because the
 * gaps are the point: what is *not* listed here is redundant or undefined, and
 * a sparse array would have to encode that distinction twice. */
static bool defined_operation(unsigned extension,
                              ap_m68882_operation_t *operation) {
  /* `$30-$37` is one instruction: "FSINCOS" occupies eight encodings because
   * its low three bits name the second destination register -- it produces two
   * results. Folding those into one entry is what stops seven of them looking
   * undefined. */
  if (extension >= 0x30u && extension <= 0x37u) {
    *operation = AP_M68882_OP_FSINCOS;
    return true;
  }

  switch (extension) {
  case 0x00u: case 0x01u: case 0x02u: case 0x03u: case 0x04u: case 0x06u:
  case 0x08u: case 0x09u: case 0x0Au: case 0x0Cu: case 0x0Du: case 0x0Eu:
  case 0x0Fu: case 0x10u: case 0x11u: case 0x12u: case 0x14u: case 0x15u:
  case 0x16u: case 0x18u: case 0x19u: case 0x1Au: case 0x1Cu: case 0x1Du:
  case 0x1Eu: case 0x1Fu: case 0x20u: case 0x21u: case 0x22u: case 0x23u:
  case 0x24u: case 0x25u: case 0x26u: case 0x27u: case 0x28u: case 0x38u:
  case 0x3Au:
    *operation = (ap_m68882_operation_t)extension;
    return true;
  default:
    break;
  }
  return false;
}

/* Footnote 3's list, verbatim: "$05, $07, $0B, $13, $17, $1B, $29-$2F, $39, and
 * $3B-$3F". These are "redundant with valid instructions implemented by the
 * FPCP, and do not cause an F-line exception if executed" -- so they are not
 * undefined, and a decoder that trapped them would fault on code the hardware
 * runs. */
static bool redundant_encoding(unsigned extension) {
  if (extension >= 0x29u && extension <= 0x2Fu) {
    return true;
  }
  if (extension >= 0x3Bu && extension <= 0x3Fu) {
    return true;
  }
  switch (extension) {
  case 0x05u: case 0x07u: case 0x0Bu: case 0x13u: case 0x17u: case 0x1Bu:
  case 0x39u:
    return true;
  default:
    break;
  }
  return false;
}

ap_m68882_command_word_t ap_m68882_decode_command(uint16_t word) {
  ap_m68882_command_word_t out = {0};
  out.opclass = (ap_m68882_opclass_t)((word >> 13) & 7u);
  out.rx = (unsigned)((word >> 10) & 7u);
  out.ry = (unsigned)((word >> 7) & 7u);
  out.extension = (unsigned)(word & 0x7Fu);

  if (defined_operation(out.extension, &out.operation)) {
    out.extension_class = AP_M68882_EXTENSION_DEFINED;
  } else if (redundant_encoding(out.extension)) {
    out.extension_class = AP_M68882_EXTENSION_REDUNDANT;
  } else {
    /* Everything left is `$40-$7F` and the handful below it the table leaves
     * out of both lists. Footnote 2: an F-line emulator trap, vector 11. */
    out.extension_class = AP_M68882_EXTENSION_UNDEFINED;
  }
  return out;
}

bool ap_m68882_command_uses_memory(const ap_m68882_command_word_t *command) {
  switch (command->opclass) {
  case AP_M68882_OPCLASS_MEMORY_TO_REGISTER:
    /* "010 111" is move constant, which reads the FPCP's own ROM and touches no
     * memory at all -- the one opclass whose answer depends on RX. Treating the
     * whole opclass as external would evaluate an effective address for an
     * instruction whose operand is a constant. */
    return command->rx != 7u;
  case AP_M68882_OPCLASS_REGISTER_TO_MEMORY:
  case AP_M68882_OPCLASS_MOVE_TO_CONTROL:
  case AP_M68882_OPCLASS_MOVE_FROM_CONTROL:
  case AP_M68882_OPCLASS_MOVEM_TO_REGISTERS:
  case AP_M68882_OPCLASS_MOVEM_FROM_REGISTERS:
    return true;
  case AP_M68882_OPCLASS_REGISTER:
  case AP_M68882_OPCLASS_RESERVED_1:
    break;
  }
  return false;
}
