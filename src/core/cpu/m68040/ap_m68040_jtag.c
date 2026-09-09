/* MC68040 IEEE 1149.1A test access port, `[040]` §6. See the header for the two
 * facts in this section that are not about JTAG. */

#include <stddef.h>

#include "cpu/m68040/ap_m68040_jtag.h"

ap_m68040_jtag_register_t
ap_m68040_jtag_data_register(ap_m68040_jtag_instruction_t instruction) {
  /* Table 6-1's last column: the three that scan pins take the boundary scan
   * register, the rest take the single-cell bypass. */
  switch (instruction) {
  case AP_M68040_JTAG_EXTEST:
  case AP_M68040_JTAG_SAMPLE_PRELOAD:
  case AP_M68040_JTAG_DRVCTL_T:
  case AP_M68040_JTAG_DRVCTL_S:
    return AP_M68040_JTAG_REG_BOUNDARY_SCAN;
  case AP_M68040_JTAG_HIGHZ:
  case AP_M68040_JTAG_SHUTDOWN:
  case AP_M68040_JTAG_PRIVATE:
  case AP_M68040_JTAG_BYPASS:
    return AP_M68040_JTAG_REG_BYPASS;
  }
  return AP_M68040_JTAG_REG_BYPASS;
}

bool ap_m68040_jtag_may_stop_clocks(ap_m68040_jtag_instruction_t instruction) {
  /* "The system clocks (PCLK and BCLK) cannot be stopped or allowed to run
   * slower than the specified frequency except when the EXTEST, HIGHZ,
   * DRVCTL.T, or SHUTDOWN instructions have been properly invoked." PRIVATE
   * shares their entry restriction but §6.4 does not license stopping the
   * clocks under it, so it is excluded. */
  switch (instruction) {
  case AP_M68040_JTAG_EXTEST:
  case AP_M68040_JTAG_HIGHZ:
  case AP_M68040_JTAG_DRVCTL_T:
  case AP_M68040_JTAG_SHUTDOWN:
    return true;
  case AP_M68040_JTAG_SAMPLE_PRELOAD:
  case AP_M68040_JTAG_PRIVATE:
  case AP_M68040_JTAG_DRVCTL_S:
  case AP_M68040_JTAG_BYPASS:
    return false;
  }
  return false;
}

bool ap_m68040_jtag_takes_over_pins(ap_m68040_jtag_instruction_t instruction) {
  /* Each of these "asserts internal system reset, activates an internal keep
   * alive clock", and "the test logic, not the system logic, has control of the
   * I/O ports". DRVCTL.S is the deliberate exception: "this instruction does
   * not invoke the internal keep alive clock, it does not assert the internal
   * reset, and the system logic, not the test logic, has control of the I/O
   * pins" -- which is the whole difference between the two DRVCTL forms. */
  switch (instruction) {
  case AP_M68040_JTAG_EXTEST:
  case AP_M68040_JTAG_HIGHZ:
  case AP_M68040_JTAG_DRVCTL_T:
  case AP_M68040_JTAG_SHUTDOWN:
    return true;
  case AP_M68040_JTAG_SAMPLE_PRELOAD:
  case AP_M68040_JTAG_DRVCTL_S:
  case AP_M68040_JTAG_BYPASS:
    return false;
  case AP_M68040_JTAG_PRIVATE:
    /* "Motorola reserves this instruction for manufacturing use. The
     * instruction does not change pin I/O as defined for system operation." */
    return false;
  }
  return false;
}

/* Table 6-2, transcribed. Bit 0 is the cell nearest `TDO` and shifts out
 * first. The order is not the pin order: A10-A31 come before the data bus,
 * A9-A0 after it, and the data bus keeps all thirty-two output cells together
 * before all thirty-two input cells where the address bus alternates them. */
static const ap_m68040_bs_bit_t boundary_scan[AP_M68040_JTAG_BS_BITS] = {
    [0] = {"RSTO", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [1] = {"IPEND", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [2] = {"CIOUT", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [3] = {"UPA0", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [4] = {"UPA1", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [5] = {"TT0", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [6] = {"TT0", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [7] = {"TT1", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [8] = {"TT1", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [9] = {"A10", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [10] = {"A10", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [11] = {"A11", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [12] = {"A11", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [13] = {"A12", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [14] = {"A12", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [15] = {"A13", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [16] = {"A13", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [17] = {"A14", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [18] = {"A14", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [19] = {"A15", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [20] = {"A15", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [21] = {"A16", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [22] = {"A16", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [23] = {"A17", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [24] = {"A17", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [25] = {"A18", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [26] = {"A18", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [27] = {"A19", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [28] = {"A19", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [29] = {"A20", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [30] = {"A20", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [31] = {"A21", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [32] = {"A21", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [33] = {"A22", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [34] = {"A22", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [35] = {"A23", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [36] = {"A23", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [37] = {"A24", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [38] = {"A24", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [39] = {"A25", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [40] = {"A25", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [41] = {"A26", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [42] = {"A26", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [43] = {"A27", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [44] = {"A27", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [45] = {"A28", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [46] = {"A28", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [47] = {"A29", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [48] = {"A29", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [49] = {"A30", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [50] = {"A30", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [51] = {"A31", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [52] = {"A31", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [53] = {"D0", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [54] = {"D1", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [55] = {"D2", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [56] = {"D3", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [57] = {"D4", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [58] = {"D5", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [59] = {"D6", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [60] = {"D7", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [61] = {"D8", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [62] = {"D9", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [63] = {"D10", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [64] = {"D11", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [65] = {"D12", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [66] = {"D13", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [67] = {"D14", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [68] = {"D15", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [69] = {"D16", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [70] = {"D17", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [71] = {"D18", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [72] = {"D19", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [73] = {"D20", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [74] = {"D21", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [75] = {"D22", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [76] = {"D23", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [77] = {"D24", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [78] = {"D25", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [79] = {"D26", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [80] = {"D27", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [81] = {"D28", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [82] = {"D29", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [83] = {"D30", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [84] = {"D31", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 151},
    [85] = {"D0", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [86] = {"D1", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [87] = {"D2", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [88] = {"D3", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [89] = {"D4", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [90] = {"D5", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [91] = {"D6", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [92] = {"D7", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [93] = {"D8", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [94] = {"D9", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [95] = {"D10", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [96] = {"D11", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [97] = {"D12", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [98] = {"D13", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [99] = {"D14", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [100] = {"D15", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [101] = {"D16", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [102] = {"D17", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [103] = {"D18", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [104] = {"D19", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [105] = {"D20", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [106] = {"D21", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [107] = {"D22", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [108] = {"D23", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [109] = {"D24", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [110] = {"D25", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [111] = {"D26", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [112] = {"D27", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [113] = {"D28", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [114] = {"D29", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [115] = {"D30", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [116] = {"D31", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 151},
    [117] = {"A9", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [118] = {"A9", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [119] = {"A8", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [120] = {"A8", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [121] = {"A7", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [122] = {"A7", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [123] = {"A6", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [124] = {"A6", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [125] = {"A5", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [126] = {"A5", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [127] = {"A4", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [128] = {"A4", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [129] = {"A3", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [130] = {"A3", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [131] = {"A2", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [132] = {"A2", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [133] = {"A1", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [134] = {"A1", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [135] = {"A0", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 150},
    [136] = {"A0", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 150},
    [137] = {"TM2", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [138] = {"TM1", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [139] = {"TM0", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [140] = {"TLN1", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [141] = {"TLN0", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 156},
    [142] = {"SIZ0", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [143] = {"SIZ0", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [144] = {"R/W", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [145] = {"R/W", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [146] = {"LOCKE", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 155},
    [147] = {"SIZ1", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [148] = {"SIZ1", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [149] = {"LOCK", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 155},
    [150] = {"io.ab", AP_M68040_BS_IO_CTL, AP_M68040_BS_CONTROL, -1},
    [151] = {"io.db", AP_M68040_BS_IO_CTL, AP_M68040_BS_CONTROL, -1},
    [152] = {"MI", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [153] = {"BR", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [154] = {"io.2", AP_M68040_BS_IO_CTL, AP_M68040_BS_CONTROL, -1},
    [155] = {"io.1", AP_M68040_BS_IO_CTL, AP_M68040_BS_CONTROL, -1},
    [156] = {"io.0", AP_M68040_BS_IO_CTL, AP_M68040_BS_CONTROL, -1},
    [157] = {"TS", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 156},
    [158] = {"TS", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 156},
    [159] = {"BB", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 155},
    [160] = {"BB", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 155},
    [161] = {"TIP", AP_M68040_BS_O_LATCH, AP_M68040_BS_TS_OUTPUT, 155},
    [162] = {"PST3", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [163] = {"PST2", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [164] = {"PST1", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [165] = {"PST0", AP_M68040_BS_O_LATCH, AP_M68040_BS_OUTPUT, -1},
    [166] = {"TA", AP_M68040_BS_O_LATCH, AP_M68040_BS_IO, 154},
    [167] = {"TA", AP_M68040_BS_I_PIN, AP_M68040_BS_IO, 154},
    [168] = {"TEA", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [169] = {"BG", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [170] = {"SC1", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [171] = {"SC0", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [172] = {"TBI", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [173] = {"AVEC", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [174] = {"TCI", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [175] = {"DLE", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [176] = {"PCLK", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [177] = {"BCLK", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [178] = {"IPL0", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [179] = {"IPL1", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [180] = {"IPL2", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [181] = {"RSTI", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [182] = {"CDIS", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},
    [183] = {"MDIS", AP_M68040_BS_I_PIN, AP_M68040_BS_INPUT, -1},};

const ap_m68040_bs_bit_t *ap_m68040_jtag_boundary_scan(void) {
  return boundary_scan;
}

const ap_m68040_bs_bit_t *ap_m68040_jtag_bs_bit(unsigned bit) {
  if (bit >= AP_M68040_JTAG_BS_BITS) {
    return NULL;
  }
  return &boundary_scan[bit];
}

bool ap_m68040_jtag_bs_is_control_cell(unsigned bit) {
  const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(bit);
  return cell != NULL && cell->cell == AP_M68040_BS_IO_CTL;
}

bool ap_m68040_jtag_bs_selects_driver(unsigned bit) {
  /* Note 2 marks the pin-type column of every `O.Latch` row and of no other,
   * so the drive control instructions reach exactly the output cells. Asserted
   * against the table in the suite rather than assumed here. */
  const ap_m68040_bs_bit_t *cell = ap_m68040_jtag_bs_bit(bit);
  return cell != NULL && cell->cell == AP_M68040_BS_O_LATCH;
}
