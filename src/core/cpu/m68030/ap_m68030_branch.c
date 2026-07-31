/* MC68030 branch family decode. See ap_m68030_branch.h for why the branch base
 * and the BSR return address are different addresses. */

#include "cpu/m68030/ap_m68030_branch.h"

ap_m68030_branch_t ap_m68030_branch_decode(uint16_t instruction) {
  ap_m68030_branch_t branch = {0};
  branch.condition = (ap_m68030_cond_t)((instruction >> 8) & 0xFu);

  /* "*Not available for the Bcc instruction": in this family the T and F
   * encodings are BRA and BSR instead. */
  branch.is_bra = (branch.condition == AP_M68030_COND_T);
  branch.is_bsr = (branch.condition == AP_M68030_COND_F);

  const uint8_t displacement = (uint8_t)(instruction & 0xFFu);
  if (displacement == 0x00u) {
    branch.size = AP_M68030_BRANCH_16BIT;
  } else if (displacement == 0xFFu) {
    branch.size = AP_M68030_BRANCH_32BIT;
  } else {
    branch.size = AP_M68030_BRANCH_8BIT;
    branch.displacement8 = (int8_t)displacement;
  }
  return branch;
}

unsigned ap_m68030_branch_length(const ap_m68030_branch_t *branch) {
  switch (branch->size) {
  case AP_M68030_BRANCH_8BIT:
    return 2;
  case AP_M68030_BRANCH_16BIT:
    return 4;
  case AP_M68030_BRANCH_32BIT:
    return 6;
  }
  return 2;
}

uint32_t ap_m68030_branch_target(uint32_t instruction_address,
                                 int32_t displacement) {
  /* The base is the instruction address plus two -- the position of the first
   * extension word -- whatever the displacement size. Wrapping is deliberate:
   * the PC is 32 bits and a branch past either end wraps on the real part. */
  return (uint32_t)(instruction_address + 2u + (uint32_t)displacement);
}

uint32_t ap_m68030_branch_return_address(uint32_t instruction_address,
                                         const ap_m68030_branch_t *branch) {
  /* The instruction that follows, not the branch base. These coincide only for
   * the 8-bit form. */
  return instruction_address + ap_m68030_branch_length(branch);
}
