/* MC68030 MOVE and MOVEA. See ap_m68030_move.h for the reversed destination
 * field and the out-of-order size encoding. */

#include "cpu/m68030/ap_m68030_move.h"

#include "cpu/m68030/ap_m68030_opcode.h"

ap_m68030_move_t ap_m68030_move_decode(uint16_t instruction) {
  ap_m68030_move_t move = {.kind = AP_M68030_MOVE_INVALID};

  /* The size *is* the low half of the family number, so the operation code map
   * answers this rather than a second table that could disagree with it. */
  const ap_m68030_opcode_family_t family = ap_m68030_opcode_family(instruction);
  const unsigned size = ap_m68030_opcode_move_size(family);
  if (size == 0u) {
    return move;
  }
  move.size = size;

  /* Source: MODE then REGISTER, the usual order. */
  move.source = ap_m68030_ea_decode((unsigned)((instruction >> 3) & 0x7u),
                                    (unsigned)(instruction & 0x7u));

  /* Destination: REGISTER then MODE -- reversed. Reading these the same way
   * round as the source produces a plausible wrong instruction, not a fault. */
  const unsigned destination_register = (unsigned)((instruction >> 9) & 0x7u);
  const unsigned destination_mode = (unsigned)((instruction >> 6) & 0x7u);
  move.destination =
      ap_m68030_ea_decode(destination_mode, destination_register);

  if (move.source.kind == AP_M68030_EA_INVALID ||
      move.destination.kind == AP_M68030_EA_INVALID) {
    move.kind = AP_M68030_MOVE_INVALID;
    return move;
  }

  if (move.destination.kind == AP_M68030_EA_ADDRESS_REGISTER) {
    /* "MOVEA ... 0 0 1" in the destination mode field. There is no byte
     * MOVEA, so a byte-sized one is not an instruction. */
    if (move.size == 1u) {
      move.kind = AP_M68030_MOVE_INVALID;
      return move;
    }
    move.kind = AP_M68030_MOVE_TO_ADDRESS_REGISTER;
    return move;
  }

  move.kind = AP_M68030_MOVE_ORDINARY;
  return move;
}

bool ap_m68030_move_affects_condition_codes(const ap_m68030_move_t *move) {
  /* MOVEA leaves them alone; MOVE sets N and Z and clears V and C. */
  return move->kind == AP_M68030_MOVE_ORDINARY;
}
