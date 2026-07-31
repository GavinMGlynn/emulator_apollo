/* MC68030 effective address calculation.
 *
 * `M68000 Family Programmer's Reference Manual 1992` §2.2, one section per
 * mode. `ap_m68030_ea` decodes the fields; this turns them into an address, and
 * applies the register side effects the increment modes have.
 *
 * ## The stack pointer is not an ordinary address register
 *
 * "If the address register is the stack pointer and the operand size is byte,
 * the address is incremented by two to keep the stack pointer aligned to a word
 * boundary" -- and the same, decremented, for predecrement. So `(A7)+` on a
 * byte moves the stack by **two**, not one.
 *
 * A model that treats A7 as just another register keeps working: the stack
 * merely drifts odd, and then every later word and long access to it is
 * misaligned. On a 68000 that faults immediately; on a 68030 it does not, so
 * the symptom is silent corruption a long way from the cause. This is the one
 * rule in the module worth knowing by heart.
 *
 * ## PC-relative modes are relative to the *extension word*
 *
 * Not to the instruction word, and not to the next instruction. The base is the
 * address of the extension word containing the displacement -- the same base
 * `Bcc` uses, and for the same reason.
 *
 * ## Memory indirect modes are not calculated here
 *
 * The full-format modes with an indirect action need a bus read partway through
 * the calculation. That belongs with the instruction unit, which owns the bus,
 * so this reports the address it reached and says indirection is outstanding
 * rather than silently returning a half-computed address.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_ADDR_H
#define APOLLO_CPU_M68030_AP_M68030_ADDR_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/m68030/ap_m68030_ea.h"
#include "cpu/m68030/ap_m68030_regs.h"

typedef struct {
  bool in_register;          /* the operand *is* a register, not memory */
  bool address_register;     /* which kind, when in_register */
  unsigned reg;

  bool immediate;            /* the operand follows the instruction */
  uint32_t address;          /* the effective address, for memory modes */

  bool indirection_pending;  /* a memory indirect action this cannot perform */
  bool valid;
} ap_m68030_address_t;

/* Everything the calculation may need, gathered so the modes that ignore most
 * of it do not each grow their own entry point. */
typedef struct {
  unsigned operand_size;      /* bytes; decides the increment modes' step */
  uint32_t extension_address; /* address of the first extension word */
  uint16_t extension_word;    /* the indexed modes' brief or full format word */
  int32_t displacement;       /* a fetched 16-bit displacement, or an absolute */
  int32_t base_displacement;  /* the full format's base displacement */
} ap_m68030_address_input_t;

/* Calculate, applying the increment and decrement side effects to `regs`. */
[[nodiscard]] ap_m68030_address_t
ap_m68030_address_calculate(ap_m68030_regs_t *regs, ap_m68030_ea_t ea,
                            const ap_m68030_address_input_t *input);

/* The step an increment mode takes for this register and operand size, which is
 * two rather than one for a byte on the stack pointer. Exposed because the
 * MOVEM instruction applies it itself, register by register. */
[[nodiscard]] unsigned ap_m68030_address_step(unsigned reg,
                                              unsigned operand_size);

#endif /* APOLLO_CPU_M68030_AP_M68030_ADDR_H */
