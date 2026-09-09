/* MC68040 external signals: the encodings of `[040]` §5.
 *
 * `MC68040 User's Manual (1993)` §5, Tables 5-1 through 5-7, read whole.
 *
 * §5 is *Signal Description*, so what it yields is encodings and reset
 * strapping rather than a state machine; the cycle those signals move through
 * is §7 Bus Operation, which §5 defers to by name eleven times. This module is
 * the encodings.
 *
 * ## The function code is gone from the bus, and TM2-TM0 replaced it
 *
 * This is the finding of §5 and it is a divergence from both earlier parts in
 * this core. A 68020 or 68030 drives FC2-FC0 on every cycle; the 68040 drives
 * **TT1-TT0** (Table 5-2, what kind of transfer) and **TM2-TM0** (Table 5-3,
 * what the transfer is for), and the two together carry what one field used to:
 *
 *     FC   68020/68030            68040, TT = 00 or 01, TM2-TM0 (Table 5-3)
 *     0    (undefined, reserved)  Data Cache Push Access
 *     1    User Data              User Data Access
 *     2    User Code              User Code Access
 *     3    (undefined, reserved)  MMU Table Search Data Access
 *     4    (undefined, reserved)  MMU Table Search Code Access
 *     5    Supervisor Data        Supervisor Data Access
 *     6    Supervisor Code        Supervisor Code Access
 *     7    **CPU Space**          **Reserved**
 *
 * The four codes a program can select are numerically unchanged, which is what
 * makes this easy to model wrongly: the three the earlier parts left undefined
 * now name real internal activity, and **CPU space no longer exists as a
 * transfer modifier**. Interrupt and breakpoint acknowledge moved to their own
 * transfer type -- `TT = 11`, with "the TMx signals carry the interrupt level
 * being acknowledged" -- and `MOVES` to function codes 0, 3, 4 and 7 moved to
 * `TT = 10` with Table 5-4's separate encoding.
 *
 * For this core that matters at the board, not in the CPU: a DN-series board
 * decodes an interrupt acknowledge out of FC7 plus the address, and a 68040
 * board cannot. It is recorded here because the DN5500 item will meet it.
 *
 * ## Three pins are strapping options at reset
 *
 * Sampled at reset and meaning something else entirely from what they mean
 * afterwards, which is a class of trap this core has met before:
 *
 *   - `CDIS`  high = normal bus, low = **multiplexed bus mode** (address and
 *             data physically tied together), §5.1, §5.2, §5.7.1.
 *   - `MDIS`  high = normal, low = **DLE mode**, in which the memory interface
 *             says when to latch read data instead of the processor latching on
 *             `BCLK`, §5.10.
 *   - `IPL2-IPL0` not an interrupt level at all but **output buffer sizing**,
 *             one pin per signal group, Table 5-5. High selects the small
 *             buffers, low the large ones.
 *
 * ## `MI` is the one output reset does not negate
 *
 * §5.7.2: `RSTI` sets all three-state signals high-impedance and negates "all
 * outputs, except MI". §5.5.2 says why -- "MI is asserted during reset
 * preventing external memory from responding" -- so a part held in reset keeps
 * alternate masters away from memory it may hold dirty lines for. A model that
 * negates everything at reset gets this backwards.
 *
 * ## Table 5-7's notes were not revised for the V parts
 *
 * Two signals are described twice with different scopes:
 *
 *     DLE   Table 5-1 note 1 "only available on the MC68040"
 *           §5.11 heading   "ONLY ON MC68040"
 *           Table 5-7 note 1 "not available on the MC68LC040 and MC68EC040"
 *     MDIS  Table 5-1 note 2 "not available on the MC68EC040 and the MC68EC040V"
 *           §5.10 heading   "NOT ON MC68EC040"
 *           Table 5-7 note 3 "not available on the MC68EC040"
 *
 * Each disagreement is about the MC68040V and MC68EC040V, and in each the
 * *shorter* statement omits them. Those parts are demonstrably later additions
 * to this edition -- Table 5-6's encoding 6 is "MC68040V and MC68EC040V only",
 * Table 5-2's acknowledge access carries "LPSTOP broadcast cycles on the
 * MC68040V and MC68EC040V", and Table 5-1 alone carries a fourth note for them.
 * So Table 5-1 is the revised text and the other two are stale, and this module
 * follows Table 5-1. Settled from inside the document, without a second manual.
 *
 * ## There is no `HALT` pin
 *
 * Neither Table 5-1 nor Table 5-7 lists one, where the 68020 and 68030 both
 * carry a bidirectional `HALT` that the processor drives when a double bus
 * fault stops it. The condition has not gone away -- it is Table 5-6's
 * encoding 5, "Halted State (Double Bus Fault)" -- but a board that watched one
 * pin for it on an earlier part has to decode four status pins here.
 *
 * ## What §5 does *not* settle
 *
 * `SIZ1-SIZ0`: §5.3.6 says only "refer to Section 7 Bus Operation for more
 * information on the encoding of these signals" -- the one encoding table §5
 * points at rather than printing. It is not modelled here and §7's walk owes
 * it.
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_SIGNALS_H
#define APOLLO_CPU_M68040_AP_M68040_SIGNALS_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Transfer type, Table 5-2.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_TT_NORMAL = 0,     /* 00 */
  AP_M68040_TT_MOVE16 = 1,     /* 01 */
  AP_M68040_TT_ALTERNATE = 2,  /* 10, alternate logical function code access */
  AP_M68040_TT_ACKNOWLEDGE = 3 /* 11, interrupt and breakpoint acknowledge */
} ap_m68040_transfer_type_t;

/* "During bus transfers by an alternate bus master, the processor samples these
 * signals to determine if it should snoop the transfer; only normal and MOVE16
 * accesses can be snooped." So an alternate master's `MOVES` or acknowledge
 * transfer passes the caches untouched however `SCx` is driven. */
[[nodiscard]] bool ap_m68040_transfer_is_snoopable(ap_m68040_transfer_type_t tt);

/* ---------------------------------------------------------------------------
 * Transfer modifier, Tables 5-3 and 5-4.
 * ------------------------------------------------------------------------- */

/* Table 5-3, for `TT` = normal or MOVE16. The numeric values are the encoding,
 * so four of them coincide with the earlier parts' function codes -- see the
 * header for the whole comparison and for what became of code 7. */
typedef enum {
  AP_M68040_TM_DATA_CACHE_PUSH = 0,
  AP_M68040_TM_USER_DATA = 1,
  AP_M68040_TM_USER_CODE = 2,
  AP_M68040_TM_TABLE_SEARCH_DATA = 3,
  AP_M68040_TM_TABLE_SEARCH_CODE = 4,
  AP_M68040_TM_SUPERVISOR_DATA = 5,
  AP_M68040_TM_SUPERVISOR_CODE = 6,
  AP_M68040_TM_RESERVED = 7
} ap_m68040_transfer_modifier_t;

/* "MOVE16 accesses use only these encodings" -- the two starred rows, user data
 * and supervisor data. `MOVE16` moves a line and a line is data, so the code
 * and table-search modifiers cannot arise for it. */
[[nodiscard]] bool
ap_m68040_modifier_valid_for_move16(ap_m68040_transfer_modifier_t tm);

/* Whether this modifier names activity the processor originates rather than a
 * program's access: a push, or either half of a table search. These three are
 * what the earlier parts left undefined, and they are the reason a 68040 bus
 * trace shows traffic no instruction asked for. */
[[nodiscard]] bool
ap_m68040_modifier_is_internal(ap_m68040_transfer_modifier_t tm);

/* Table 5-4, for `TT` = 10. The alternate access carries only the four logical
 * function codes that are *not* reachable as a normal access -- 0, 3, 4 and
 * 7 -- and the modifier value equals the function code. Returns false and
 * leaves `*fc` alone for the four reserved encodings. */
[[nodiscard]] bool ap_m68040_alternate_function_code(unsigned tm, unsigned *fc);

/* ---------------------------------------------------------------------------
 * Reset strapping, §5.7.1, §5.8.1 and §5.10.
 * ------------------------------------------------------------------------- */

/* Table 5-5: at reset each `IPLx` pin latches the drive strength of one signal
 * group. "High input level = small buffers enabled; low input level = large
 * buffers enabled." */
typedef enum {
  AP_M68040_DRIVER_GROUP_DATA_BUS,          /* IPL2 */
  AP_M68040_DRIVER_GROUP_ADDRESS_AND_ATTRS, /* IPL1 */
  AP_M68040_DRIVER_GROUP_MISC_CONTROL       /* IPL0 */
} ap_m68040_driver_group_t;

/* Which group a given `IPLx` pin controls. `ipl` is 2, 1 or 0. */
[[nodiscard]] ap_m68040_driver_group_t
ap_m68040_driver_group_for_ipl(unsigned ipl);

/* The latched reset options. Modelled as one struct because all three are
 * sampled at the same edge and none of them can change afterwards. */
typedef struct {
  /* `CDIS` low at reset: address and data bus physically tied together. */
  bool multiplexed_bus;
  /* `MDIS` low at reset: the memory interface latches read data with `DLE`. */
  bool dle_mode;
  /* One per group, indexed by `ap_m68040_driver_group_t`. True is the large
   * buffer, which is the *low* input level. */
  bool large_buffers[3];
} ap_m68040_reset_options_t;

/* Latch the three strapping inputs. `ipl` is IPL2-IPL0 as a three-bit value at
 * the pins, so it is active low twice over: low selects the large buffer. */
[[nodiscard]] ap_m68040_reset_options_t
ap_m68040_latch_reset_options(bool cdis, bool mdis, unsigned ipl);

/* ---------------------------------------------------------------------------
 * Processor status, Table 5-6.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_PST_USER_START = 0x0,
  AP_M68040_PST_USER_END = 0x1,
  AP_M68040_PST_USER_BRANCH_NOT_TAKEN = 0x2,
  AP_M68040_PST_USER_BRANCH_TAKEN = 0x3,
  AP_M68040_PST_USER_TABLE_SEARCH = 0x4,
  AP_M68040_PST_HALTED = 0x5, /* double bus fault */
  AP_M68040_PST_LOW_POWER_STOP = 0x6, /* MC68040V and MC68EC040V only */
  AP_M68040_PST_RESERVED = 0x7,
  AP_M68040_PST_SUPERVISOR_START = 0x8,
  AP_M68040_PST_SUPERVISOR_END = 0x9,
  AP_M68040_PST_SUPERVISOR_BRANCH_NOT_TAKEN = 0xA,
  AP_M68040_PST_SUPERVISOR_BRANCH_TAKEN = 0xB,
  AP_M68040_PST_SUPERVISOR_TABLE_SEARCH = 0xC,
  AP_M68040_PST_STOPPED = 0xD,
  AP_M68040_PST_RTE_EXECUTING = 0xE,
  AP_M68040_PST_EXCEPTION_STACKING = 0xF
} ap_m68040_pst_t;

/* §5.9.1 splits the encodings into two classes: "the encodings 0, 8, 4, 5, C,
 * D, E, and F indicate the present status ... These encodings persist as long
 * as the processor stays in the indicated state", against "the encodings 1, 2,
 * 3, 9, A, and B ... exist for only one BCLK period per instruction, and are
 * mutually exclusive".
 *
 * That list covers fourteen of the sixteen and **omits 6 and 7**. 7 is
 * Reserved; 6 is Low-Power Stop Mode on the V parts, which is a state the
 * processor stays in and so belongs to the persisting class on its own terms.
 * The omission is one more sign the V parts were added late -- see the header.
 * Reported as persisting, with the manual's silence recorded here. */
[[nodiscard]] bool ap_m68040_pst_persists(ap_m68040_pst_t pst);

/* Whether §5.9.1's own classification names this encoding at all. False for 6
 * and 7 only. */
[[nodiscard]] bool ap_m68040_pst_is_classified(ap_m68040_pst_t pst);

/* True for the six encodings that mark an instruction boundary -- the ones
 * §5.9.1 calls the first class. `IPEND` exists so "external devices (other bus
 * masters) can use IPEND to predict processor operation on the next instruction
 * boundaries", and these are those boundaries. */
[[nodiscard]] bool ap_m68040_pst_ends_instruction(ap_m68040_pst_t pst);

/* True in supervisor state. `PST3` alone carries it for the twelve encodings
 * that have a privilege at all; the four that do not -- halted, low-power stop,
 * stopped, reserved -- are not a privilege level and report false. */
[[nodiscard]] bool ap_m68040_pst_is_supervisor(ap_m68040_pst_t pst);

/* ---------------------------------------------------------------------------
 * Signal summary, Tables 5-1 and 5-7.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_SIGNAL_INPUT,
  AP_M68040_SIGNAL_OUTPUT,
  AP_M68040_SIGNAL_BIDIRECTIONAL,
  AP_M68040_SIGNAL_POWER
} ap_m68040_signal_type_t;

typedef enum {
  AP_M68040_ACTIVE_HIGH,
  AP_M68040_ACTIVE_LOW,
  AP_M68040_ACTIVE_BOTH, /* R/W: "High/Low" -- high reads, low writes */
  AP_M68040_ACTIVE_NONE  /* clocks, power and ground */
} ap_m68040_signal_active_t;

/* Which members of the family carry a signal. `[040]` §5 states availability as
 * a list of exceptions per signal; this is that list turned around. */
typedef enum {
  AP_M68040_PART_ALL,
  AP_M68040_PART_MC68040_ONLY,     /* DLE */
  AP_M68040_PART_NOT_EC040_FAMILY, /* MDIS: not on MC68EC040 or MC68EC040V */
  AP_M68040_PART_NOT_V_PARTS       /* PCLK, TRST: not on MC68040V, MC68EC040V */
} ap_m68040_signal_scope_t;

typedef struct {
  const char *mnemonic;
  const char *name;
  ap_m68040_signal_type_t type;
  ap_m68040_signal_active_t active;
  bool three_state;
  ap_m68040_signal_scope_t scope;
} ap_m68040_signal_t;

/* Table 5-7, forty rows, with availability taken from Table 5-1's notes. */
#define AP_M68040_SIGNAL_COUNT 40u

[[nodiscard]] const ap_m68040_signal_t *ap_m68040_signals(void);

/* Look up one row by mnemonic, or NULL. */
[[nodiscard]] const ap_m68040_signal_t *ap_m68040_signal(const char *mnemonic);

#endif /* APOLLO_CPU_M68040_AP_M68040_SIGNALS_H */
