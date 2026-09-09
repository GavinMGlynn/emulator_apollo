/* MC68040 IEEE 1149.1A test access port: the encodings of `[040]` §6.
 *
 * `MC68040 User's Manual (1993)` §6, Tables 6-1 and 6-2, and the BSDL listing
 * of §6.6, read whole.
 *
 * Nothing in this machine drives JTAG -- a DN-series board has no TAP
 * controller on it, and this core has no consumer for a boundary scan. §6 is
 * modelled because it is a finished specification with two tables in it and
 * because two of its facts are not about JTAG at all:
 *
 *   - **The drive control latches are loaded from `IPL2-IPL0` at the negation
 *     of `RSTI`**, and §6.2.7 adds the number §5 does not: this happens "after
 *     RSTI has been negated, and the **128-clock internal reset cycle** has
 *     expired". §5's Table 5-5 says which pin sizes which group; §6 says when.
 *   - **The device has no internal power-up reset circuit.** §6.4 says so
 *     outright, of `TRST`: "the TRST signal should be treated similar to the
 *     RSTI signal for board design considerations concerning power-up
 *     conditions". A board must drive both.
 *
 * ## §6 covers three of the five parts, and that explains a note in §5
 *
 * §6 opens with a NOTE: "this section does not apply to the MC68040V and
 * MC68EC040V ... all references to M68040 in this section only, refer to the
 * MC68040, MC68LC040, and MC68EC040." Its Table 6-2 then carries note 5,
 * "renamed JS0 on the MC68LC040 and MC68EC040" -- **the identical pair as
 * Table 5-7's note 1**, and correct here because §6 has excluded the V parts.
 *
 * That is worth having, because §5's walk had to choose between Table 5-1 and
 * Table 5-7 on exactly this pair and inferred that Table 5-7 was a survival of
 * an edition written before the V parts existed. §6 shows that pair is
 * precisely the pre-V-part family, stated openly, so the inference was sound
 * after all; §1.1 separately gives the mechanism (the pins are renamed rather
 * than removed). All three readings agree and answer different questions --
 * *which* table to follow, *why* that list of parts, and *what* physically
 * differs. See `ap_m68040_signals.h` and `ap_m68040_family.h`.
 *
 * ## Capture-IR loads the HIGHZ opcode on purpose
 *
 * "During the capture-IR state, the binary value 001 is loaded", and 001 is
 * `HIGHZ`. §6.2.2 says this is deliberate: "using only the TMS and TCK pins and
 * the capture-IR and update-IR states invokes the HIGHZ instruction. This
 * scheme works because the value captured by the instruction shift register
 * during the capture-IR state is identical to the HIGHZ opcode." So a board can
 * float every output driver without a `TDI` connection.
 *
 * Test-logic-reset sets the register to all ones, which is `BYPASS`.
 *
 * ## Stopping the system clocks can destroy the part
 *
 * §6.2 and §6.4 both say it: the system clocks may only be stopped under
 * `EXTEST`, `HIGHZ`, `DRVCTL.T` or `SHUTDOWN`, and even then "PCLK and BCLK
 * must be kept running for **two additional BCLK periods** upon initial entry".
 * Under any other instruction "failure to do so could result in potential
 * internal damage to the device". `ap_m68040_jtag_may_stop_clocks` is that
 * rule.
 *
 * ## The BSDL is mask-set specific, and one bit moved
 *
 * §6.6's listing is "for the newer MC68040 mask sets of E26A and after (roughly
 * after the second half of 1992). It does not include the 0.8-um mask sets
 * D43B, D50D, and D98D", and its own revision list opens with "LOCK and LOCKE
 * controlled by io.1 vice io.0 (4D98D)". So on the older masks boundary scan
 * bits 146 and 149 were controlled by `io.0`, not `io.1`. Table 6-2 gives the
 * newer arrangement and that is what is modelled; the older one is recorded
 * here rather than in code because §6 gives no other detail about those masks.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_JTAG_H
#define APOLLO_CPU_M68040_AP_M68040_JTAG_H

#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Instruction shift register, Table 6-1.
 * ------------------------------------------------------------------------- */

/* Three bits, no parity. "The least significant bit of the instruction (bit 0)
 * is the first bit to be shifted into the instruction shift register." */
typedef enum {
  AP_M68040_JTAG_EXTEST = 0x0,
  AP_M68040_JTAG_HIGHZ = 0x1,
  AP_M68040_JTAG_SAMPLE_PRELOAD = 0x2,
  AP_M68040_JTAG_DRVCTL_T = 0x3,
  AP_M68040_JTAG_SHUTDOWN = 0x4,
  AP_M68040_JTAG_PRIVATE = 0x5,
  AP_M68040_JTAG_DRVCTL_S = 0x6,
  AP_M68040_JTAG_BYPASS = 0x7
} ap_m68040_jtag_instruction_t;

#define AP_M68040_JTAG_IR_BITS 3u
/* "The instruction shift register is reset to all ones in the TAP controller
 * test-logic-reset state, which is equivalent to selecting the BYPASS
 * instruction." */
#define AP_M68040_JTAG_IR_RESET AP_M68040_JTAG_BYPASS
/* "During the capture-IR state, the binary value 001 is loaded" -- the HIGHZ
 * opcode, deliberately. */
#define AP_M68040_JTAG_IR_CAPTURE AP_M68040_JTAG_HIGHZ

/* Which test data register an instruction selects. Table 6-1's last column. */
typedef enum {
  AP_M68040_JTAG_REG_BOUNDARY_SCAN,
  AP_M68040_JTAG_REG_BYPASS
} ap_m68040_jtag_register_t;

[[nodiscard]] ap_m68040_jtag_register_t
ap_m68040_jtag_data_register(ap_m68040_jtag_instruction_t instruction);

/* Whether `PCLK` and `BCLK` may be stopped under this instruction -- after two
 * further `BCLK` periods on entry. False for the other four, where stopping
 * them risks "potential internal damage to the device". */
[[nodiscard]] bool
ap_m68040_jtag_may_stop_clocks(ap_m68040_jtag_instruction_t instruction);

/* Whether the instruction asserts internal system reset and runs the keep-alive
 * clock, taking the I/O pins away from the system logic. This is what separates
 * `DRVCTL.T` from `DRVCTL.S`, which are otherwise the same function. */
[[nodiscard]] bool
ap_m68040_jtag_takes_over_pins(ap_m68040_jtag_instruction_t instruction);

/* ---------------------------------------------------------------------------
 * Boundary scan register, Table 6-2.
 * ------------------------------------------------------------------------- */

#define AP_M68040_JTAG_BS_BITS 184u

typedef enum {
  AP_M68040_BS_O_LATCH, /* BSDL BC_2, an output cell */
  AP_M68040_BS_I_PIN,   /* BSDL BC_4, an input cell */
  AP_M68040_BS_IO_CTL   /* BSDL BC_2, a direction control cell */
} ap_m68040_bs_cell_t;

typedef enum {
  AP_M68040_BS_OUTPUT,    /* output only; HIGHZ can float it (note 3) */
  AP_M68040_BS_TS_OUTPUT, /* three-state output */
  AP_M68040_BS_IO,        /* bidirectional */
  AP_M68040_BS_INPUT,
  AP_M68040_BS_CONTROL /* the cell is not a pin at all */
} ap_m68040_bs_pin_type_t;

typedef struct {
  const char *pin;
  ap_m68040_bs_cell_t cell;
  ap_m68040_bs_pin_type_t pin_type;
  /* The boundary scan bit of the control cell that enables this pin's output,
   * or -1 where the table prints a note instead: output-only pins, which HIGHZ
   * floats, and the five control cells themselves. */
  int control_bit;
} ap_m68040_bs_bit_t;

[[nodiscard]] const ap_m68040_bs_bit_t *ap_m68040_jtag_boundary_scan(void);

/* "The instruction shift register cell nearest TDO (i.e. first to be shifted
 * out) is defined as bit zero. The last bit to be shifted out is bit 183." */
[[nodiscard]] const ap_m68040_bs_bit_t *ap_m68040_jtag_bs_bit(unsigned bit);

/* The five direction control cells, in the order §6.3 lists them. */
#define AP_M68040_JTAG_BS_CONTROL_CELLS 5u
[[nodiscard]] bool ap_m68040_jtag_bs_is_control_cell(unsigned bit);

/* Whether a bit participates in the drive control instructions -- Table 6-2's
 * note 2, "boundary scan register bit positions that are used during the drive
 * control (DRVCTL.X) instructions". "A logic zero in the appropriate boundary
 * scan output cell selects the large buffer, and a logic one selects the small
 * buffer", which is the same polarity as Table 5-5's `IPLx` strapping. */
[[nodiscard]] bool ap_m68040_jtag_bs_selects_driver(unsigned bit);

#endif /* APOLLO_CPU_M68040_AP_M68040_JTAG_H */
