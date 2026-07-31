/* MC68030 addressing mode categories. See ap_m68030_category.h for why these
 * are derived from §2.3's definitions rather than transcribed from Table 2-4,
 * whose Alterable column is shifted in the scan. */

#include "cpu/m68030/ap_m68030_category.h"

bool ap_m68030_ea_is_data(ap_m68030_ea_kind_t kind) {
  /* "Data addressing modes refer to data operands." An address register holds
   * an address, not a data operand -- which is why ADD accepts every mode but
   * An, and ADDA exists for that one. */
  switch (kind) {
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_INVALID:
    return false;
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
  case AP_M68030_EA_PC_DISPLACEMENT:
  case AP_M68030_EA_PC_INDEXED:
  case AP_M68030_EA_IMMEDIATE:
    return true;
  }
  return false;
}

bool ap_m68030_ea_is_memory(ap_m68030_ea_kind_t kind) {
  /* "Memory addressing modes refer to memory operands", so the two register
   * direct modes are out and everything else -- the immediate included, since
   * it is fetched from the instruction stream -- is in. */
  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_INVALID:
    return false;
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
  case AP_M68030_EA_PC_DISPLACEMENT:
  case AP_M68030_EA_PC_INDEXED:
  case AP_M68030_EA_IMMEDIATE:
    return true;
  }
  return false;
}

bool ap_m68030_ea_is_control(ap_m68030_ea_kind_t kind) {
  /* "Control addressing modes refer to memory operands without an associated
   * size." The increment modes carry one -- their step *is* the operand size --
   * and so does the immediate, whose length is the operand size. That is the
   * whole of the difference between control and memory, and it is why `JMP
   * (A0)+` does not exist: there is no size to step by. */
  switch (kind) {
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    return false;
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
  case AP_M68030_EA_PC_DISPLACEMENT:
  case AP_M68030_EA_PC_INDEXED:
    return true;
  }
  return false;
}

bool ap_m68030_ea_is_alterable(ap_m68030_ea_kind_t kind) {
  /* "Alterable addressing modes refer to alterable (writable) operands."
   * Nothing PC-relative is writable -- the program counter is not a base a
   * store may use on this architecture -- and neither is an immediate, which
   * lives in the instruction stream. Everything else is. */
  switch (kind) {
  case AP_M68030_EA_PC_DISPLACEMENT:
  case AP_M68030_EA_PC_INDEXED:
  case AP_M68030_EA_IMMEDIATE:
  case AP_M68030_EA_INVALID:
    return false;
  case AP_M68030_EA_DATA_REGISTER:
  case AP_M68030_EA_ADDRESS_REGISTER:
  case AP_M68030_EA_ADDRESS_INDIRECT:
  case AP_M68030_EA_POSTINCREMENT:
  case AP_M68030_EA_PREDECREMENT:
  case AP_M68030_EA_DISPLACEMENT:
  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
    return true;
  }
  return false;
}

bool ap_m68030_ea_is_data_alterable(ap_m68030_ea_kind_t kind) {
  return ap_m68030_ea_is_data(kind) && ap_m68030_ea_is_alterable(kind);
}

bool ap_m68030_ea_is_memory_alterable(ap_m68030_ea_kind_t kind) {
  return ap_m68030_ea_is_memory(kind) && ap_m68030_ea_is_alterable(kind);
}

bool ap_m68030_ea_is_control_alterable(ap_m68030_ea_kind_t kind) {
  return ap_m68030_ea_is_control(kind) && ap_m68030_ea_is_alterable(kind);
}
