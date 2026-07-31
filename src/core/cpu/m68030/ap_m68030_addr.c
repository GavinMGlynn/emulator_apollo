/* MC68030 effective address calculation. See ap_m68030_addr.h for the stack
 * pointer's byte rule and for what is deliberately not calculated here. */

#include "cpu/m68030/ap_m68030_addr.h"

unsigned ap_m68030_address_step(unsigned reg, unsigned operand_size) {
  /* "If the address register is the stack pointer and the operand size is byte,
   * the address is incremented by two to keep the stack pointer aligned to a
   * word boundary." A7 is the stack pointer whichever of the three it currently
   * names, so the test is on the register number. */
  if (reg == 7u && operand_size == 1u) {
    return 2;
  }
  return operand_size;
}

/* The index operand of the indexed modes: a data or address register, taken as
 * a sign-extended word or a full long, then scaled. */
static int32_t index_value(const ap_m68030_regs_t *regs,
                           const ap_m68030_extension_t *extension) {
  const uint32_t raw =
      extension->index_is_address_register
          ? ap_m68030_read_address_register(regs, extension->index_register)
          : regs->d[extension->index_register];

  const int32_t sized = extension->index_long
                            ? (int32_t)raw
                            : (int32_t)(int16_t)(uint16_t)(raw & 0xFFFFu);
  return sized * (int32_t)extension->scale;
}

ap_m68030_address_t
ap_m68030_address_calculate(ap_m68030_regs_t *regs, ap_m68030_ea_t ea,
                            const ap_m68030_address_input_t *input) {
  ap_m68030_address_t out = {.valid = true};

  switch (ea.kind) {
  case AP_M68030_EA_DATA_REGISTER:
    out.in_register = true;
    out.reg = ea.reg;
    return out;

  case AP_M68030_EA_ADDRESS_REGISTER:
    out.in_register = true;
    out.address_register = true;
    out.reg = ea.reg;
    return out;

  case AP_M68030_EA_ADDRESS_INDIRECT:
    out.address = ap_m68030_read_address_register(regs, ea.reg);
    return out;

  case AP_M68030_EA_POSTINCREMENT: {
    /* "EA = (An); An + SIZE -> An" -- the address is taken *before* the
     * register moves. */
    out.address = ap_m68030_read_address_register(regs, ea.reg);
    const unsigned step = ap_m68030_address_step(ea.reg, input->operand_size);
    ap_m68030_write_address_register(regs, ea.reg, out.address + step);
    return out;
  }

  case AP_M68030_EA_PREDECREMENT: {
    /* "An - SIZE -> An; EA = (An)" -- the register moves *first*. */
    const unsigned step = ap_m68030_address_step(ea.reg, input->operand_size);
    const uint32_t moved =
        ap_m68030_read_address_register(regs, ea.reg) - step;
    ap_m68030_write_address_register(regs, ea.reg, moved);
    out.address = moved;
    return out;
  }

  case AP_M68030_EA_DISPLACEMENT:
    /* "EA = (An) + d16" */
    out.address = ap_m68030_read_address_register(regs, ea.reg) +
                  (uint32_t)input->displacement;
    return out;

  case AP_M68030_EA_PC_DISPLACEMENT:
    /* Relative to the extension word, not to the instruction word. */
    out.address = input->extension_address + (uint32_t)input->displacement;
    return out;

  case AP_M68030_EA_ABSOLUTE_SHORT:
  case AP_M68030_EA_ABSOLUTE_LONG:
    /* The caller has already sign-extended a short absolute. */
    out.address = (uint32_t)input->displacement;
    return out;

  case AP_M68030_EA_IMMEDIATE:
    out.immediate = true;
    return out;

  case AP_M68030_EA_INDEXED:
  case AP_M68030_EA_PC_INDEXED: {
    const ap_m68030_extension_t extension =
        ap_m68030_ea_decode_extension(input->extension_word);

    if (extension.reserved) {
      out.valid = false;
      return out;
    }

    /* The base is the address register, or the extension word's address for the
     * PC forms -- and either may be suppressed by BS in the full format. */
    uint32_t base = 0;
    if (!(extension.full_format && extension.base_suppressed)) {
      base = (ea.kind == AP_M68030_EA_PC_INDEXED)
                 ? input->extension_address
                 : ap_m68030_read_address_register(regs, ea.reg);
    }

    int32_t index = 0;
    if (!(extension.full_format && extension.index_suppressed)) {
      index = index_value(regs, &extension);
    }

    const int32_t displacement =
        extension.full_format ? input->base_displacement
                              : (int32_t)extension.displacement;

    const bool indirect =
        extension.full_format && extension.indirect != AP_M68030_INDIRECT_NONE;

    /* "EA = (An + bd) + Xn.SIZE*SCALE + od" for the postindexed mode: the
     * processor "calculates an intermediate indirect memory address using a
     * base address register and base displacement", *without* the index, and
     * adds the index to what it reads. The preindexed mode puts the index
     * inside instead -- "using a base address register, a base displacement,
     * and the index operand" -- which is what the brackets in each assembler
     * syntax say. */
    const bool index_after =
        indirect && extension.indirect == AP_M68030_INDIRECT_POSTINDEXED;

    out.address = base + (uint32_t)displacement +
                  (index_after ? 0u : (uint32_t)index);

    /* A memory indirect action needs a bus read partway through, which belongs
     * with the instruction unit. Report the intermediate address and what is
     * still owed, rather than returning a half-computed address as though it
     * were final. */
    if (indirect) {
      out.indirection_pending = true;
      out.post_indirection =
          input->outer_displacement + (index_after ? index : 0);
    }
    return out;
  }

  case AP_M68030_EA_INVALID:
    out.valid = false;
    return out;
  }

  out.valid = false;
  return out;
}
