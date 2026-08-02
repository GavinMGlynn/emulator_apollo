/* MC68851 status and protection registers. See ap_m68851_regs.h; Figures 6-2
 * and 6-4 through 6-7 read from the page images. */

#include "cpu/m68851/ap_m68851_regs.h"

ap_m68851_pcsr_t ap_m68851_pcsr_decode(uint16_t value) {
  return (ap_m68851_pcsr_t){
      .flush = (value & 0x8000u) != 0u,
      .lock_warning = (value & 0x4000u) != 0u,
      .task_alias = (unsigned)(value & 0x7u),
  };
}

uint16_t ap_m68851_pcsr_encode(const ap_m68851_pcsr_t *pcsr) {
  uint16_t value = (uint16_t)(pcsr->task_alias & 0x7u);
  if (pcsr->flush) {
    value |= 0x8000u;
  }
  if (pcsr->lock_warning) {
    value |= 0x4000u;
  }
  return value;
}

ap_m68851_psr_t ap_m68851_psr_decode(uint16_t value) {
  return (ap_m68851_psr_t){
      .bus_error = (value & 0x8000u) != 0u,
      .limit_violation = (value & 0x4000u) != 0u,
      .supervisor_only = (value & 0x2000u) != 0u,
      .access_level_violation = (value & 0x1000u) != 0u,
      .write_protected = (value & 0x0800u) != 0u,
      .invalid = (value & 0x0400u) != 0u,
      .modified = (value & 0x0200u) != 0u,
      .gate = (value & 0x0100u) != 0u,
      .globally_sharable = (value & 0x0080u) != 0u,
      .levels = (unsigned)(value & 0x7u),
  };
}

uint16_t ap_m68851_psr_encode(const ap_m68851_psr_t *psr) {
  uint16_t value = (uint16_t)(psr->levels & 0x7u);
  if (psr->bus_error) {
    value |= 0x8000u;
  }
  if (psr->limit_violation) {
    value |= 0x4000u;
  }
  if (psr->supervisor_only) {
    value |= 0x2000u;
  }
  if (psr->access_level_violation) {
    value |= 0x1000u;
  }
  if (psr->write_protected) {
    value |= 0x0800u;
  }
  if (psr->invalid) {
    value |= 0x0400u;
  }
  if (psr->modified) {
    value |= 0x0200u;
  }
  if (psr->gate) {
    value |= 0x0100u;
  }
  if (psr->globally_sharable) {
    value |= 0x0080u;
  }
  return value;
}

ap_m68851_ac_t ap_m68851_ac_decode(uint16_t value) {
  return (ap_m68851_ac_t){
      .module_control = (value & 0x0080u) != 0u,
      .access_level_control = (ap_m68851_alc_t)((value >> 4) & 0x3u),
      .module_descriptor_size = (ap_m68851_mds_t)(value & 0x3u),
  };
}

uint16_t ap_m68851_ac_encode(const ap_m68851_ac_t *ac) {
  uint16_t value = 0;
  if (ac->module_control) {
    value |= 0x0080u;
  }
  value |= (uint16_t)(((unsigned)ac->access_level_control & 0x3u) << 4);
  value |= (uint16_t)((unsigned)ac->module_descriptor_size & 0x3u);
  return value;
}

unsigned ap_m68851_ac_access_levels(const ap_m68851_ac_t *ac) {
  switch (ac->access_level_control) {
  case AP_M68851_ALC_DISABLED:
    /* "No Address Bits Used: Access Level Checking is Disabled." Zero levels,
     * not one: the mechanism is off rather than collapsed to a single level. */
    return 0u;
  case AP_M68851_ALC_ONE_BIT:
    return 2u;
  case AP_M68851_ALC_TWO_BITS:
    return 4u;
  case AP_M68851_ALC_THREE_BITS:
    return 8u;
  }
  return 0u;
}

uint32_t ap_m68851_ac_module_descriptor_alignment(const ap_m68851_ac_t *ac) {
  switch (ac->module_descriptor_size) {
  case AP_M68851_MDS_ALL_INVALID:
    return 0u;
  case AP_M68851_MDS_16_BYTE:
    return 16u;
  case AP_M68851_MDS_32_BYTE:
    return 32u;
  case AP_M68851_MDS_64_BYTE:
    return 64u;
  }
  return 0u;
}

bool ap_m68851_ac_module_descriptor_aligned(const ap_m68851_ac_t *ac,
                                            uint32_t address) {
  const uint32_t alignment = ap_m68851_ac_module_descriptor_alignment(ac);
  if (alignment == 0u) {
    /* "$0 -- All Module Descriptors are Invalid." No address passes, which is
     * how the mechanism is disabled -- not by accepting every address. */
    return false;
  }
  return (address % alignment) == 0u;
}

unsigned ap_m68851_access_level_decode(uint8_t value) {
  /* "Only the upper three bits are implemented." */
  return (unsigned)((value >> 5) & 0x7u);
}

uint8_t ap_m68851_access_level_encode(unsigned level) {
  return (uint8_t)((level & 0x7u) << 5);
}

bool ap_m68851_scc_changes_stack(uint8_t scc, unsigned current,
                                 unsigned target) {
  /* "where m < n (greater privilege)". A call that does not increase privilege
   * falls outside the rule and changes no stack. */
  if (target >= current) {
    return false;
  }
  /* "if any bit of SCC between n and m (inclusive) is set" -- a range test over
   * the bitmap, so an intermediate level's bit forces the change even when
   * neither endpoint's does. */
  for (unsigned level = target; level <= current && level < 8u; level++) {
    if ((scc & (1u << level)) != 0u) {
      return true;
    }
  }
  return false;
}
