/* MC68040 bus operation, `[040]` §7. See the header for what §7 owed the three
 * sections that defer to it, and for why Table 7-6 is not keyed as printed. */

#include <stddef.h>

#include "cpu/m68040/ap_m68040_bus.h"

/* ---------------------------------------------------------------------------
 * Transfer size and byte lanes, Table 7-1.
 * ------------------------------------------------------------------------- */

/* Bit 0 is D31-D24, bit 3 is D7-D0. */
#define LANE_UU 0x8u
#define LANE_UM 0x4u
#define LANE_LM 0x2u
#define LANE_LL 0x1u
#define LANE_ALL (LANE_UU | LANE_UM | LANE_LM | LANE_LL)

unsigned ap_m68040_byte_lanes(ap_m68040_size_t size, unsigned offset) {
  const unsigned a1 = (offset >> 1) & 1u;
  const unsigned a0 = offset & 1u;
  switch (size) {
  case AP_M68040_SIZE_BYTE:
    /* One lane, selected by the byte offset. */
    if (a1 == 0u) {
      return a0 == 0u ? LANE_UU : LANE_UM;
    }
    return a0 == 0u ? LANE_LM : LANE_LL;
  case AP_M68040_SIZE_WORD:
    if (a0 != 0u) {
      /* Not a row of Table 7-1 -- see the header. A misaligned word is split
       * into two byte transfers before it reaches the bus. */
      return 0u;
    }
    return a1 == 0u ? (LANE_UU | LANE_UM) : (LANE_LM | LANE_LL);
  case AP_M68040_SIZE_LONG:
  case AP_M68040_SIZE_LINE:
    /* "A1 and A0 = X"; the selected device should ignore them. */
    return LANE_ALL;
  }
  return 0u;
}

unsigned ap_m68040_bus_cycles(ap_m68040_size_t size, unsigned offset) {
  /* Table 7-3, for noncachable and write-through accesses. A line transfer is
   * not in the table: it is one bus cycle by construction, burst or not, and
   * §7.4.2 counts it in clocks instead. */
  static const unsigned cycles[4][4] = {
      [AP_M68040_SIZE_BYTE] = {1u, 1u, 1u, 1u},
      [AP_M68040_SIZE_WORD] = {1u, 2u, 1u, 2u},
      [AP_M68040_SIZE_LONG] = {1u, 3u, 2u, 3u},
      [AP_M68040_SIZE_LINE] = {1u, 1u, 1u, 1u},
  };
  return cycles[(unsigned)size & 3u][offset & 3u];
}

unsigned ap_m68040_instruction_bus_cycles(unsigned offset) {
  /* "N/A" for every offset but zero: "the processor always prefetches
   * instructions by reading a long word from a half-line address (A2-A0 = $0),
   * regardless of alignment", so no other offset occurs. */
  return (offset & 3u) == 0u ? 1u : 0u;
}

/* ---------------------------------------------------------------------------
 * Access types, Table 7-2.
 * ------------------------------------------------------------------------- */

#define S(x) (1u << (unsigned)(AP_M68040_SIZE_##x))

static const ap_m68040_access_info_t accesses[AP_M68040_ACCESS_COUNT] = {
    [AP_M68040_ACCESS_DATA_CACHE_PUSH] =
        {
            .name = "Data Cache Push",
            .transfer_type = 0u,
            .address_is_fixed = false,
            .upa = AP_M68040_SIGNAL_NEGATED,
            .ciout = AP_M68040_SIGNAL_NEGATED,
            /* "L/Line" -- a single dirty long word is pushed as a long word,
             * two or more as a line. §4.6.2 says the same. */
            .sizes = S(LONG) | S(LINE),
            .tln_defined = true,
            .may_lock = false,
            .read_only = false,
        },
    [AP_M68040_ACCESS_NORMAL] =
        {
            .name = "Normal Data/Code",
            .transfer_type = 0u,
            .address_is_fixed = false,
            .upa = AP_M68040_SIGNAL_FROM_MMU,
            .ciout = AP_M68040_SIGNAL_FROM_MMU,
            .sizes = S(BYTE) | S(WORD) | S(LONG) | S(LINE),
            /* Note 2 restricts TLNx to pushes and normal data *line* reads, so
             * it is defined for this access type only in that case. */
            .tln_defined = true,
            .may_lock = true,
            .read_only = false,
        },
    [AP_M68040_ACCESS_TABLE_SEARCH] =
        {
            .name = "Table Search",
            .transfer_type = 0u,
            .address_is_fixed = false,
            .upa = AP_M68040_SIGNAL_NEGATED,
            .ciout = AP_M68040_SIGNAL_NEGATED,
            .sizes = S(LONG),
            .tln_defined = false,
            /* "Some page descriptor updates during translation table searches
             * also use read-modify-write transfers." */
            .may_lock = true,
            .read_only = false,
        },
    [AP_M68040_ACCESS_MOVE16] =
        {
            .name = "MOVE16",
            .transfer_type = 1u,
            .address_is_fixed = false,
            .upa = AP_M68040_SIGNAL_FROM_MMU,
            .ciout = AP_M68040_SIGNAL_FROM_MMU,
            .sizes = S(LINE),
            .tln_defined = false,
            .may_lock = false,
            .read_only = false,
        },
    [AP_M68040_ACCESS_ALTERNATE] =
        {
            .name = "Alternate",
            .transfer_type = 2u,
            .address_is_fixed = false,
            .upa = AP_M68040_SIGNAL_NEGATED,
            /* Asserted unconditionally: a `MOVES` to an alternate function code
             * is implicitly noncachable, which §7.4.1 lists among the accesses
             * that never reach a cache. */
            .ciout = AP_M68040_SIGNAL_ASSERTED,
            .sizes = S(BYTE) | S(WORD) | S(LONG),
            .tln_defined = false,
            .may_lock = false,
            .read_only = false,
        },
    [AP_M68040_ACCESS_INTERRUPT_ACK] =
        {
            .name = "Interrupt Acknowledge",
            .transfer_type = 3u,
            .address_is_fixed = true,
            .address = 0xFFFFFFFFu,
            .upa = AP_M68040_SIGNAL_NEGATED,
            .ciout = AP_M68040_SIGNAL_NEGATED,
            .sizes = S(BYTE),
            .tln_defined = false,
            .may_lock = false,
            .read_only = true,
        },
    [AP_M68040_ACCESS_BREAKPOINT_ACK] =
        {
            .name = "Breakpoint Acknowledge",
            .transfer_type = 3u,
            .address_is_fixed = true,
            .address = 0x00000000u,
            .upa = AP_M68040_SIGNAL_NEGATED,
            .ciout = AP_M68040_SIGNAL_NEGATED,
            .sizes = S(BYTE),
            .tln_defined = false,
            .may_lock = false,
            .read_only = true,
        },
    [AP_M68040_ACCESS_LPSTOP_BROADCAST] =
        {
            .name = "LPSTOP Broadcast",
            /* Table C-2: TT1,TT0 = $3, the same acknowledge type as the other
             * two fixed-address cycles. */
            .transfer_type = 3u,
            .address_is_fixed = true,
            .address = 0xFFFFFFFEu,
            .upa = AP_M68040_SIGNAL_NEGATED,
            .ciout = AP_M68040_SIGNAL_NEGATED,
            /* "SIZ1, SIZ0 = $2" -- a word, carrying the new SR on D15-D0. The
             * only acknowledge-type cycle that is not a byte. */
            .sizes = 1u << (unsigned)AP_M68040_SIZE_WORD,
            .tln_defined = false,
            .may_lock = false,
            /* "R/W = 0": a write, unlike both other acknowledge cycles. */
            .read_only = false,
        },
};

#undef S

const ap_m68040_access_info_t *ap_m68040_access(ap_m68040_access_t access) {
  if (access >= AP_M68040_ACCESS_COUNT) {
    return NULL;
  }
  return &accesses[access];
}

/* ---------------------------------------------------------------------------
 * Terminations, Tables 7-4 and 7-5.
 * ------------------------------------------------------------------------- */

ap_m68040_termination_t ap_m68040_terminate(bool ta, bool tea) {
  /* Table 7-5's four cases. "To properly control termination of a bus cycle for
   * a bus error or retry condition, TA and TEA must be asserted and negated for
   * the same rising edge of BCLK." */
  if (tea) {
    return ta ? AP_M68040_TERM_RETRY : AP_M68040_TERM_BUS_ERROR;
  }
  return ta ? AP_M68040_TERM_NORMAL : AP_M68040_TERM_WAIT;
}

ap_m68040_iack_result_t ap_m68040_iack_terminate(bool ta, bool tea,
                                                 bool avec) {
  /* Table 7-4. `AVEC` is "Don't Care" on three of the five rows, and §7.5.1.2
   * says why: "AVEC is only sampled with TA asserted." */
  if (tea) {
    return ta ? AP_M68040_IACK_RETRY : AP_M68040_IACK_SPURIOUS;
  }
  if (!ta) {
    return AP_M68040_IACK_WAIT;
  }
  return avec ? AP_M68040_IACK_AUTOVECTOR : AP_M68040_IACK_VECTOR;
}

unsigned ap_m68040_autovector(unsigned level) {
  /* "There are seven distinct autovectors that can be used, corresponding to
   * the seven levels of interrupts available with IPL2-IPL0", so level 0 is not
   * one of them and would return the spurious vector's number. */
  if (level == 0u || level > 7u) {
    return AP_M68040_SPURIOUS_VECTOR;
  }
  return AP_M68040_AUTOVECTOR_BASE + level;
}

/* ---------------------------------------------------------------------------
 * Bus arbitration, Table 7-6.
 * ------------------------------------------------------------------------- */

ap_m68040_arbitration_t ap_m68040_arbitration_state(bool bg, bool drives_bb,
                                                    bool bus_driven,
                                                    bool defined_values,
                                                    bool snooping) {
  if (!bg) {
    /* "The idle state occurs when the M68040 does not have ownership of the bus
     * and is not in the process of snooping an access ... The snoop state is
     * similar to the idle state in that the M68040 does not have ownership of
     * the bus", differing only in readiness to service a snooped transfer. */
    return snooping ? AP_M68040_ARB_SNOOP : AP_M68040_ARB_IDLE;
  }
  if (!drives_bb) {
    /* `BG` asserted and `BB` three-stated is either implicit ownership -- the
     * grant with nothing pending -- or an alternate master driving `BB` itself.
     * Table 7-6 separates them by whether the M68040 drives the bus. */
    return bus_driven ? AP_M68040_ARB_IMPLICIT_OWNERSHIP
                      : AP_M68040_ARB_ALTERNATE_MASTER;
  }
  /* The M68040 asserts `BB`: an active cycle drives "defined values", park
   * drives undefined ones. Both assert `TIP`. */
  return defined_values ? AP_M68040_ARB_ACTIVE_BUS_CYCLE : AP_M68040_ARB_PARK;
}

const char *ap_m68040_nonallocating_name(ap_m68040_nonallocating_t which) {
  static const char *const names[AP_M68040_NONALLOCATING_COUNT] = {
      [AP_M68040_NONALLOCATING_TABLE_SEARCH] = "table search",
      [AP_M68040_NONALLOCATING_TABLE_UPDATE] = "table update",
      [AP_M68040_NONALLOCATING_VECTOR_FETCH] = "exception vector fetch",
      [AP_M68040_NONALLOCATING_EXCEPTION_STACKING] = "exception stacking",
      [AP_M68040_NONALLOCATING_RTE_DEALLOCATION] = "RTE stack deallocation",
  };
  if (which >= AP_M68040_NONALLOCATING_COUNT) {
    return NULL;
  }
  return names[which];
}

bool ap_m68040_is_rated_frequency(unsigned hz, bool ec_or_lc) {
  /* §11.5's three columns for the MC68040, and Table 11-4's three rows for the
   * MC68LC040 and MC68EC040. The two sets overlap at 25 and 33 and differ at
   * both ends: only the derivatives are rated at 20, and only the MC68040 at
   * 40. */
  if (hz < AP_M68040_MIN_FREQUENCY_HZ) {
    return false;
  }
  if (ec_or_lc) {
    return hz == 20000000u || hz == 25000000u || hz == 33000000u;
  }
  return hz == 25000000u || hz == 33000000u || hz == 40000000u;
}
