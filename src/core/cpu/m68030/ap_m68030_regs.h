/* MC68030 programming model: registers and the status register.
 *
 * `[030]` §1.3 and the `M68000 Family Programmer's Reference Manual` §1.3.2,
 * whose Figure 1-8 survives intact where the 68030 manual's does not.
 *
 * ## A7 is three registers wearing one name
 *
 * "Register A7 ... is a register designation that applies to the user stack
 * pointer in the user privilege level and to either the interrupt or master
 * stack pointer in the supervisor privilege level." The PRM's own table:
 *
 *     S  M   ACTIVE STACK
 *     0  x   USP
 *     1  0   ISP
 *     1  1   MSP
 *
 * Note the `x`: in user state M is *ignored*, not required to be zero. A model
 * that switches on the pair (S,M) as four cases has invented a fourth stack.
 * This is the 68020-and-later addition — on the 68000 "the M-bit is always
 * zero" and there is only one supervisor stack — so it is exactly the kind of
 * thing a 68000-shaped mental model gets wrong.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_REGS_H
#define APOLLO_CPU_M68030_AP_M68030_REGS_H

#include <stdbool.h>
#include <stdint.h>

/* Status register bit positions, from the PRM's Figure 1-8. Bits 11, 7, 6 and 5
 * are shown as zero and have no field. */
#define AP_M68030_SR_T1_BIT 15u
#define AP_M68030_SR_T0_BIT 14u
#define AP_M68030_SR_S_BIT 13u
#define AP_M68030_SR_M_BIT 12u
#define AP_M68030_SR_INTERRUPT_SHIFT 8u
#define AP_M68030_SR_INTERRUPT_MASK 0x7u
#define AP_M68030_SR_X_BIT 4u
#define AP_M68030_SR_N_BIT 3u
#define AP_M68030_SR_Z_BIT 2u
#define AP_M68030_SR_V_BIT 1u
#define AP_M68030_SR_C_BIT 0u

/* "the CCR, the status register's lower byte, is the only portion of the status
 * register (SR) available in the user mode." */
#define AP_M68030_CCR_MASK 0x001Fu

/* Every bit the SR actually defines. The complement is the reserved set, which
 * must read as zero however it is written. */
#define AP_M68030_SR_DEFINED 0xF71Fu

/* "T1 T0 TRACE MODE" from the PRM's table. `11` is documented as UNDEFINED
 * rather than as a fourth mode, and is reported as such rather than silently
 * folded into one of the other three. */
typedef enum {
  AP_M68030_TRACE_NONE = 0,           /* T1 0, T0 0 */
  AP_M68030_TRACE_ANY_INSTRUCTION,    /* T1 1, T0 0 */
  AP_M68030_TRACE_ON_CHANGE_OF_FLOW,  /* T1 0, T0 1 */
  AP_M68030_TRACE_UNDEFINED,          /* T1 1, T0 1 */
} ap_m68030_trace_mode_t;

/* Which stack pointer A7 currently names. */
typedef enum {
  AP_M68030_STACK_USP = 0,
  AP_M68030_STACK_ISP,
  AP_M68030_STACK_MSP,
} ap_m68030_stack_t;

typedef struct {
  uint32_t d[8];
  uint32_t a[7]; /* A0-A6; A7 is whichever stack pointer is active */

  uint32_t usp; /* user stack pointer */
  uint32_t isp; /* interrupt stack pointer */
  uint32_t msp; /* master stack pointer */

  uint32_t pc;
  uint16_t sr;

  uint32_t vbr;  /* vector base register */
  uint8_t sfc;   /* source function code, 3 bits */
  uint8_t dfc;   /* destination function code, 3 bits */
} ap_m68030_regs_t;

[[nodiscard]] bool ap_m68030_supervisor(const ap_m68030_regs_t *regs);
[[nodiscard]] bool ap_m68030_master(const ap_m68030_regs_t *regs);
[[nodiscard]] unsigned ap_m68030_interrupt_mask(const ap_m68030_regs_t *regs);
[[nodiscard]] ap_m68030_trace_mode_t
ap_m68030_trace_mode(const ap_m68030_regs_t *regs);

/* Which stack A7 names, per the PRM's S/M table. */
[[nodiscard]] ap_m68030_stack_t
ap_m68030_active_stack(const ap_m68030_regs_t *regs);

/* Read and write A7 through whichever stack pointer is active, which is what
 * every instruction referencing A7 does. */
[[nodiscard]] uint32_t ap_m68030_read_a7(const ap_m68030_regs_t *regs);
void ap_m68030_write_a7(ap_m68030_regs_t *regs, uint32_t value);

/* Any address register, 0-7, with 7 resolving through the active stack. */
[[nodiscard]] uint32_t ap_m68030_read_address_register(
    const ap_m68030_regs_t *regs, unsigned index);
void ap_m68030_write_address_register(ap_m68030_regs_t *regs, unsigned index,
                                      uint32_t value);

/* Write the whole SR, as a supervisor MOVE to SR does. Reserved bits are
 * discarded rather than stored: they read as zero on the real part, and keeping
 * them would let a written value read back differently from what the hardware
 * would return. */
void ap_m68030_write_sr(ap_m68030_regs_t *regs, uint16_t value);

/* Write only the condition codes, as MOVE to CCR does — "the only portion of
 * the status register (SR) available in the user mode". The system byte is
 * untouched, so this cannot be used to escalate privilege. */
void ap_m68030_write_ccr(ap_m68030_regs_t *regs, uint16_t value);

[[nodiscard]] uint16_t ap_m68030_read_ccr(const ap_m68030_regs_t *regs);

#endif /* APOLLO_CPU_M68030_AP_M68030_REGS_H */
