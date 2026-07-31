/* MC68030 programming model. See ap_m68030_regs.h for the citations and for why
 * A7 is three registers wearing one name. */

#include "cpu/m68030/ap_m68030_regs.h"

static bool sr_bit(const ap_m68030_regs_t *regs, unsigned position) {
  return ((regs->sr >> position) & 1u) != 0u;
}

bool ap_m68030_supervisor(const ap_m68030_regs_t *regs) {
  return sr_bit(regs, AP_M68030_SR_S_BIT);
}

bool ap_m68030_master(const ap_m68030_regs_t *regs) {
  return sr_bit(regs, AP_M68030_SR_M_BIT);
}

unsigned ap_m68030_interrupt_mask(const ap_m68030_regs_t *regs) {
  return (unsigned)((regs->sr >> AP_M68030_SR_INTERRUPT_SHIFT) &
                    AP_M68030_SR_INTERRUPT_MASK);
}

ap_m68030_trace_mode_t ap_m68030_trace_mode(const ap_m68030_regs_t *regs) {
  const unsigned t1 = sr_bit(regs, AP_M68030_SR_T1_BIT) ? 1u : 0u;
  const unsigned t0 = sr_bit(regs, AP_M68030_SR_T0_BIT) ? 1u : 0u;
  if (t1 && t0) {
    return AP_M68030_TRACE_UNDEFINED;
  }
  if (t1) {
    return AP_M68030_TRACE_ANY_INSTRUCTION;
  }
  if (t0) {
    return AP_M68030_TRACE_ON_CHANGE_OF_FLOW;
  }
  return AP_M68030_TRACE_NONE;
}

ap_m68030_stack_t ap_m68030_active_stack(const ap_m68030_regs_t *regs) {
  /* "S 0, M x -> USP": in user state M is ignored rather than required to be
   * zero, so this is three cases and not four. */
  if (!ap_m68030_supervisor(regs)) {
    return AP_M68030_STACK_USP;
  }
  return ap_m68030_master(regs) ? AP_M68030_STACK_MSP : AP_M68030_STACK_ISP;
}

uint32_t ap_m68030_read_a7(const ap_m68030_regs_t *regs) {
  switch (ap_m68030_active_stack(regs)) {
  case AP_M68030_STACK_USP:
    return regs->usp;
  case AP_M68030_STACK_ISP:
    return regs->isp;
  case AP_M68030_STACK_MSP:
    return regs->msp;
  }
  return regs->usp;
}

void ap_m68030_write_a7(ap_m68030_regs_t *regs, uint32_t value) {
  switch (ap_m68030_active_stack(regs)) {
  case AP_M68030_STACK_USP:
    regs->usp = value;
    return;
  case AP_M68030_STACK_ISP:
    regs->isp = value;
    return;
  case AP_M68030_STACK_MSP:
    regs->msp = value;
    return;
  }
}

uint32_t ap_m68030_read_address_register(const ap_m68030_regs_t *regs,
                                         unsigned index) {
  return (index == 7u) ? ap_m68030_read_a7(regs) : regs->a[index];
}

void ap_m68030_write_address_register(ap_m68030_regs_t *regs, unsigned index,
                                      uint32_t value) {
  if (index == 7u) {
    ap_m68030_write_a7(regs, value);
    return;
  }
  regs->a[index] = value;
}

void ap_m68030_write_sr(ap_m68030_regs_t *regs, uint16_t value) {
  /* The reserved bits "0" in Figure 1-8 are dropped rather than stored, so a
   * value written always reads back as the hardware would return it. */
  regs->sr = (uint16_t)(value & AP_M68030_SR_DEFINED);
}

void ap_m68030_write_ccr(ap_m68030_regs_t *regs, uint16_t value) {
  regs->sr = (uint16_t)((regs->sr & (uint16_t)~AP_M68030_CCR_MASK) |
                        (value & AP_M68030_CCR_MASK));
}

uint16_t ap_m68030_read_ccr(const ap_m68030_regs_t *regs) {
  return (uint16_t)(regs->sr & AP_M68030_CCR_MASK);
}
