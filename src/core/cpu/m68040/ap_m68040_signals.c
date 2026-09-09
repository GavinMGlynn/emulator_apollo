/* MC68040 external signals: the encodings of `[040]` §5. See the header for
 * what became of the function code and for which of the book's two summary
 * tables is the revised one. */

#include <string.h>

#include "cpu/m68040/ap_m68040_signals.h"

/* ---------------------------------------------------------------------------
 * Transfer type, Table 5-2.
 * ------------------------------------------------------------------------- */

bool ap_m68040_transfer_is_snoopable(ap_m68040_transfer_type_t tt) {
  return tt == AP_M68040_TT_NORMAL || tt == AP_M68040_TT_MOVE16;
}

/* ---------------------------------------------------------------------------
 * Transfer modifier, Tables 5-3 and 5-4.
 * ------------------------------------------------------------------------- */

bool ap_m68040_modifier_valid_for_move16(ap_m68040_transfer_modifier_t tm) {
  return tm == AP_M68040_TM_USER_DATA || tm == AP_M68040_TM_SUPERVISOR_DATA;
}

bool ap_m68040_modifier_is_internal(ap_m68040_transfer_modifier_t tm) {
  return tm == AP_M68040_TM_DATA_CACHE_PUSH ||
         tm == AP_M68040_TM_TABLE_SEARCH_DATA ||
         tm == AP_M68040_TM_TABLE_SEARCH_CODE;
}

bool ap_m68040_alternate_function_code(unsigned tm, unsigned *fc) {
  switch (tm & 0x7u) {
  case 0u:
  case 3u:
  case 4u:
  case 7u:
    /* "Logical Function Code 0 / 3 / 4 / 7": the modifier is the code. The
     * other four are Reserved because 1, 2, 5 and 6 are reachable as normal
     * accesses and need no alternate encoding. */
    *fc = tm & 0x7u;
    return true;
  default:
    return false;
  }
}

/* ---------------------------------------------------------------------------
 * Reset strapping, §5.7.1, §5.8.1 and §5.10.
 * ------------------------------------------------------------------------- */

ap_m68040_driver_group_t ap_m68040_driver_group_for_ipl(unsigned ipl) {
  switch (ipl) {
  case 2u:
    return AP_M68040_DRIVER_GROUP_DATA_BUS;
  case 1u:
    return AP_M68040_DRIVER_GROUP_ADDRESS_AND_ATTRS;
  default:
    return AP_M68040_DRIVER_GROUP_MISC_CONTROL;
  }
}

ap_m68040_reset_options_t ap_m68040_latch_reset_options(bool cdis, bool mdis,
                                                        unsigned ipl) {
  ap_m68040_reset_options_t options;
  memset(&options, 0, sizeof options);
  /* "During a processor reset, the level on CDIS is latched and used to select
   * the normal bus mode (CDIS high) or multiplexed bus mode (CDIS low)." */
  options.multiplexed_bus = !cdis;
  /* "During a processor reset, the level on MDIS is latched and used to select
   * the normal data latch mode (MDIS high) or DLE mode (MDIS low)." */
  options.dle_mode = !mdis;
  /* Table 5-5's note: "High input level = small buffers enabled; low input
   * level = large buffers enabled." IPL2 is the most significant bit. */
  options.large_buffers[AP_M68040_DRIVER_GROUP_DATA_BUS] =
      ((ipl >> 2) & 1u) == 0u;
  options.large_buffers[AP_M68040_DRIVER_GROUP_ADDRESS_AND_ATTRS] =
      ((ipl >> 1) & 1u) == 0u;
  options.large_buffers[AP_M68040_DRIVER_GROUP_MISC_CONTROL] =
      (ipl & 1u) == 0u;
  return options;
}

/* ---------------------------------------------------------------------------
 * Processor status, Table 5-6.
 * ------------------------------------------------------------------------- */

bool ap_m68040_pst_ends_instruction(ap_m68040_pst_t pst) {
  /* "The encodings 1, 2, 3, 9, A, and B ... indicate that the instruction is in
   * its last instruction execution stage. These encodings exist for only one
   * BCLK period per instruction, and are mutually exclusive." */
  const unsigned code = (unsigned)pst & 0xFu;
  /* 1, 2, 3, 9, A and B: the low three bits are 1, 2 or 3 and bit 2 is clear,
   * which is the same shape in both privilege halves. Written as the manual's
   * six values rather than as that observation, so a reader checks a list
   * against the page instead of trusting an arithmetic coincidence. */
  return code == 0x1u || code == 0x2u || code == 0x3u || code == 0x9u ||
         code == 0xAu || code == 0xBu;
}

bool ap_m68040_pst_is_classified(ap_m68040_pst_t pst) {
  /* §5.9.1 names fourteen of the sixteen; 6 and 7 appear in Table 5-6 and in
   * neither class list. */
  return pst != AP_M68040_PST_LOW_POWER_STOP && pst != AP_M68040_PST_RESERVED;
}

bool ap_m68040_pst_persists(ap_m68040_pst_t pst) {
  /* The second class, plus encoding 6 on the reasoning in the header. */
  return !ap_m68040_pst_ends_instruction(pst) &&
         pst != AP_M68040_PST_RESERVED;
}

bool ap_m68040_pst_is_supervisor(ap_m68040_pst_t pst) {
  const unsigned code = (unsigned)pst & 0xFu;
  /* The five rows Table 5-6 labels "Supervisor": 8, 9, A, B and C. Stopped,
   * RTE executing and exception stacking also have PST3 set and are not
   * labelled, so PST3 is not a privilege bit and this is a list, not a mask. */
  return code >= 0x8u && code <= 0xCu;
}

/* ---------------------------------------------------------------------------
 * Signal summary, Tables 5-1 and 5-7.
 * ------------------------------------------------------------------------- */

/* Table 5-7 in its own order, which is alphabetical by *name* rather than by
 * mnemonic. Availability comes from Table 5-1's notes, which are the revised
 * ones -- see the header. `IPL2-IPL0` carries a note in both tables that is not
 * an availability note at all ("these signals are different on power-up for the
 * MC68LC040 and MC68EC040"), so its scope is every part. */
static const ap_m68040_signal_t signals[AP_M68040_SIGNAL_COUNT] = {
    {"A31-A0", "Address Bus", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"AVEC", "Autovector", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_ALL},
    {"BB", "Bus Busy", AP_M68040_SIGNAL_BIDIRECTIONAL, AP_M68040_ACTIVE_LOW,
     true, AP_M68040_PART_ALL},
    {"BCLK", "Bus Clock", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_NONE, false,
     AP_M68040_PART_ALL},
    {"BG", "Bus Grant", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_ALL},
    {"BR", "Bus Request", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_ALL},
    {"CDIS", "Cache Disable", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW,
     false, AP_M68040_PART_ALL},
    {"CIOUT", "Cache Inhibit Out", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_LOW, true, AP_M68040_PART_ALL},
    {"D31-D0", "Data Bus", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"DLE", "Data Latch Enable", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_HIGH,
     false, AP_M68040_PART_MC68040_ONLY},
    {"GND", "Ground", AP_M68040_SIGNAL_POWER, AP_M68040_ACTIVE_NONE, false,
     AP_M68040_PART_ALL},
    {"IPEND", "Interrupt Pending", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_LOW, false, AP_M68040_PART_ALL},
    {"IPL2-IPL0", "Interrupt Priority Level", AP_M68040_SIGNAL_INPUT,
     AP_M68040_ACTIVE_LOW, false, AP_M68040_PART_ALL},
    {"LOCK", "Bus Lock", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_LOW, true,
     AP_M68040_PART_ALL},
    {"LOCKE", "Bus Lock End", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_LOW,
     true, AP_M68040_PART_ALL},
    {"MI", "Memory Inhibit", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_LOW,
     false, AP_M68040_PART_ALL},
    {"MDIS", "MMU Disable", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_NOT_EC040_FAMILY},
    {"PCLK", "Processor Clock", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_NONE,
     false, AP_M68040_PART_NOT_V_PARTS},
    {"PST3-PST0", "Processor Status", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_HIGH, false, AP_M68040_PART_ALL},
    {"R/W", "Read/Write", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_BOTH, true, AP_M68040_PART_ALL},
    {"RSTI", "Reset In", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_ALL},
    {"RSTO", "Reset Out", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_ALL},
    {"SC1-SC0", "Snoop Control", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_HIGH,
     false, AP_M68040_PART_ALL},
    {"TA", "Transfer Acknowledge", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_LOW, true, AP_M68040_PART_ALL},
    {"TBI", "Transfer Burst Inhibit", AP_M68040_SIGNAL_INPUT,
     AP_M68040_ACTIVE_LOW, false, AP_M68040_PART_ALL},
    {"TCI", "Transfer Cache Inhibit", AP_M68040_SIGNAL_INPUT,
     AP_M68040_ACTIVE_LOW, false, AP_M68040_PART_ALL},
    {"TEA", "Transfer Error Acknowledge", AP_M68040_SIGNAL_INPUT,
     AP_M68040_ACTIVE_LOW, false, AP_M68040_PART_ALL},
    {"TIP", "Transfer in Progress", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_LOW, true, AP_M68040_PART_ALL},
    {"TLN1-TLN0", "Transfer Line Number", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"TM2-TM0", "Transfer Modifier", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"SIZ1-SIZ0", "Transfer Size", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"TS", "Transfer Start", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_LOW, true, AP_M68040_PART_ALL},
    {"TT1-TT0", "Transfer Type", AP_M68040_SIGNAL_BIDIRECTIONAL,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"TCK", "Test Clock", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_NONE, false,
     AP_M68040_PART_ALL},
    {"TDI", "Test Data Input", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_HIGH,
     false, AP_M68040_PART_ALL},
    {"TDO", "Test Data Output", AP_M68040_SIGNAL_OUTPUT, AP_M68040_ACTIVE_HIGH,
     true, AP_M68040_PART_ALL},
    {"TMS", "Test Mode Select", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_HIGH,
     false, AP_M68040_PART_ALL},
    {"TRST", "Test Reset", AP_M68040_SIGNAL_INPUT, AP_M68040_ACTIVE_LOW, false,
     AP_M68040_PART_NOT_V_PARTS},
    {"UPA1-UPA0", "User-Programmable Attributes", AP_M68040_SIGNAL_OUTPUT,
     AP_M68040_ACTIVE_HIGH, true, AP_M68040_PART_ALL},
    {"VCC", "Power Supply", AP_M68040_SIGNAL_POWER, AP_M68040_ACTIVE_NONE,
     false, AP_M68040_PART_ALL},
};

const ap_m68040_signal_t *ap_m68040_signals(void) { return signals; }

const ap_m68040_signal_t *ap_m68040_signal(const char *mnemonic) {
  for (unsigned i = 0; i < AP_M68040_SIGNAL_COUNT; i++) {
    if (strcmp(signals[i].mnemonic, mnemonic) == 0) {
      return &signals[i];
    }
  }
  return NULL;
}
