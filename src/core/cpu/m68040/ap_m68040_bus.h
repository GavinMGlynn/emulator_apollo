/* MC68040 bus operation: the encodings and constants of `[040]` §7.
 *
 * `MC68040 User's Manual (1993)` §7, Tables 7-1 through 7-6, read whole.
 *
 * §4, §5 and §6 all defer to §7 by name, and this is what they were owing.
 * What is modelled here is §7's tables and its stated constants, not a
 * cycle-by-cycle bus state machine: there is no 68040 stepper to drive one and
 * no memory system attached to this part, so a state machine would be a model
 * of nothing. `ap_m68030_bus.*` is what a real one looks like when the rest of
 * the machine exists to justify it.
 *
 * ## `SIZ = 11` no longer means three bytes
 *
 * Table 7-1's encoding is `01` byte, `10` word, `00` long word, **`11` line**.
 * On the 68020 and 68030 the fourth encoding is a **three-byte** transfer --
 * `ap_m68030_bus.h` carries it as `AP_M68030_SIZE_THREE`. A board that decoded
 * `SIZ` for one of those parts and met a 68040 would read every 16-byte burst
 * as a three-byte transfer. The 68040 has no three-byte transfer at all: §7.2
 * says it "does not support dynamic bus sizing and expects the referenced
 * device to accept the requested access width".
 *
 * ## Two acknowledge cycles at two fixed addresses
 *
 * Table 7-2 again: an interrupt acknowledge drives `A31-A0` = **`$FFFFFFFF`**
 * with the level on `TM2-TM0`, and a breakpoint acknowledge drives
 * **`$00000000`** with `TM2-TM0` = `$0`. Both are `TT = 11`. On the earlier
 * parts both are CPU-space cycles whose *address* carries the level and the
 * type -- so a board that decodes an acknowledge out of `FC7` and the address
 * has nothing to decode here. See `ap_m68040_signals.h` for what became of the
 * function code.
 *
 * **A third one exists, on the V parts only.** Appendix C's Table C-2 gives the
 * `LPSTOP` broadcast cycle `A31-A0` = **`$FFFFFFFE`**, `TT = $3`, `TM = $0`,
 * `R/W` = write and **`SIZ = $2`, a word** -- the only acknowledge-type cycle
 * that is not a byte, carrying the new status register value on `D15-D0`. So
 * all three fixed addresses are one apart at the top of memory or at zero:
 * `$FFFFFFFF` interrupt, `$FFFFFFFE` LPSTOP, `$00000000` breakpoint. "Either
 * TA or TEA terminates the LPSTOP broadcast cycle. By withholding the assertion
 * of TA or TEA, external logic can extend the cycle, controlling the beginning
 * of the low-power stop mode."
 *
 * And a `BKPT` on this part always ends the same way: §7.5.2, "when the
 * external device terminates the cycle with either TA or TEA, the processor
 * takes an **illegal instruction exception**". The data returned is not used.
 * Table 1-4's own entry agrees -- "run breakpoint acknowledge cycle; TRAP as
 * illegal instruction".
 *
 * ## Reset is 10 clocks in and 128 clocks after
 *
 * §7.10, and these are the numbers §5 and §6 kept pointing at:
 *
 *   - `RSTI` must be asserted for **at least 10 `BCLK` cycles** -- after `Vcc`
 *     is within tolerance for a power-on reset, and for any later one.
 *   - `RSTI` is internally synchronised for **two `BCLK`s** before use.
 *   - Once `RSTI` negates the part is held in reset for **another 128 clocks**,
 *     and `CDIS`, `MDIS` and `IPL2-IPL0` "should be driven to their normal
 *     levels before the end of the 128-clock internal reset period" -- so the
 *     strapping window closes then, not at `RSTI` negation.
 *   - A `RESET` *instruction* drives `RSTO` for **512 `BCLK` cycles** and
 *     leaves the internal registers alone. An `RSTI` during it resets the
 *     processor immediately and negates `RSTO`.
 *   - **`MI` is asserted while the part is in reset** and stays asserted "during
 *     and after reset until the first bus cycle of the M68040" (§7.9.1), which
 *     is the end point §5.5.2 does not give.
 *
 * ## Table 7-6's key columns are not a key
 *
 * §7.8.1 names five arbitration states and says what separates them: "there are
 * two characteristics that determine these five states: whether the three-state
 * logic determines if the M68040 drives the bus **and how the M68040 drives
 * BB**". Table 7-6 then keys the five on the *levels* of `BB` and `BG`, and
 * that cannot work:
 *
 *   - **Park** and **Alternate Bus Master Ownership** both read `BB` asserted,
 *     `BG` asserted. They differ in who is driving `BB` -- the M68040 asserts
 *     it in park and three-states it under an alternate master -- which is a
 *     fact about the driver, not about the level.
 *   - The **Active Bus Cycle** row says `BG` **negated** in its column and
 *     "arbiter asserts `BG`" in its own Conditions cell, three words apart.
 *     The body text settles it: the processor asserts `BB` and starts the cycle
 *     *after* being granted the bus, and "as long as BG is asserted, BB remains
 *     asserted". The column is wrong and the Conditions cell is right. (The
 *     page's next paragraph, "the M68040 can be in the active bus cycle, park,
 *     or implicit ownership states when BG is negated", is about `BG` being
 *     negated *during* a cycle -- the indeterminate condition -- not about the
 *     state's defining condition.)
 *
 * So this module keys the states on what §7.8.1 says determines them: whether
 * the M68040 drives the bus, whether it drives `BB`, and whether the bus is
 * driven with defined values. `ap_m68040_arbitration_state()` takes those.
 *
 * ## The locked sequence is divisible here
 *
 * §7.8.1: "the read and write portions of a locked read-modify-write sequence
 * are divisible in the M68040, allowing the bus to be arbitrated away during the
 * locked sequence." `LOCK` is a request to the arbiter, not a hold on the bus,
 * and an arbiter that must not break a locked sequence has to watch `LOCK`
 * itself. `LOCKE` marks the last transfer so two back-to-back locked sequences
 * can be arbitrated between -- and §7.8.2.1 notes the corollary: a system that
 * supports relinquish-and-retry on the last write of a locked transfer **cannot
 * use `LOCKE` at all**, because the arbiter would hand the bus over between the
 * retry and the write.
 *
 * ## `CAS` and `CAS2` write even when they fail
 *
 * §7.4.5: "the read-modify-write transfer for the CAS and CAS2 instructions in
 * the M68040 differs from those used by previous members of the M68000 family.
 * If an operand does not match ... the M68040 **still executes a single write
 * transfer** to terminate the locked sequence with LOCKE asserted. For the CAS
 * instruction, the value read from memory is written back; for the CAS2
 * instruction, the second operand read is written back." A failed compare is
 * still a bus write, and it still dirties a cache line.
 *
 * ## `NOP` is a synchronising instruction
 *
 * §7.7: writes can be deferred indefinitely and reads can pass them -- "a given
 * sequence of read accesses or write accesses is completed in order, and
 * reordering only occurs with writes relative to reads" -- and "the NOP
 * instruction forces instruction and bus synchronization because it freezes
 * instruction execution until all pending bus cycles have completed". That is
 * why `NOP` appears in §5.9.1's list of instructions that end with a
 * branch-taken status encoding. A page marked **serialized noncachable** does
 * the same thing for reads only: "the definition of a page as noncachable
 * versus serialized noncachable only affects read accesses".
 */

#ifndef APOLLO_CPU_M68040_AP_M68040_BUS_H
#define APOLLO_CPU_M68040_AP_M68040_BUS_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Transfer size and byte lanes, Table 7-1.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_SIZE_LONG = 0, /* 00 */
  AP_M68040_SIZE_BYTE = 1, /* 01 */
  AP_M68040_SIZE_WORD = 2, /* 10 */
  AP_M68040_SIZE_LINE = 3  /* 11, three bytes on the 68020 and 68030 */
} ap_m68040_size_t;

/* Which byte lanes carry data, as a four-bit mask. Bit 0 is `D31-D24`, bit 3 is
 * `D7-D0`, matching Table 7-1's column order. `offset` is A1 and A0.
 *
 * Zero for a word at an odd byte offset: §7.3 says the data memory unit
 * "converts misaligned operand accesses that are noncachable to a sequence of
 * aligned accesses", and Figure 7-6 shows that sequence is two byte transfers,
 * so that combination never reaches the bus and Table 7-1 does not list it. */
[[nodiscard]] unsigned ap_m68040_byte_lanes(ap_m68040_size_t size,
                                            unsigned offset);

/* Table 7-3, the bus cycles a noncachable or write-through access costs at each
 * byte offset. Zero where the table prints N/A: an instruction fetch is only
 * ever made from a half-line boundary, so no other offset arises. */
[[nodiscard]] unsigned ap_m68040_bus_cycles(ap_m68040_size_t size,
                                            unsigned offset);
[[nodiscard]] unsigned ap_m68040_instruction_bus_cycles(unsigned offset);

/* ---------------------------------------------------------------------------
 * Access types, Table 7-2.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_ACCESS_DATA_CACHE_PUSH,
  AP_M68040_ACCESS_NORMAL,
  AP_M68040_ACCESS_TABLE_SEARCH,
  AP_M68040_ACCESS_MOVE16,
  AP_M68040_ACCESS_ALTERNATE,
  AP_M68040_ACCESS_INTERRUPT_ACK,
  AP_M68040_ACCESS_BREAKPOINT_ACK,
  /* Appendix C, Table C-2. Only the MC68040V and MC68EC040V issue it, and it
   * is a third fixed-address cycle sharing `TT = 11` with the other two. */
  AP_M68040_ACCESS_LPSTOP_BROADCAST,
  AP_M68040_ACCESS_COUNT
} ap_m68040_access_t;

/* Where Table 7-2 prints "MMU Source", the value comes from the page
 * descriptor rather than from the access type. */
typedef enum {
  AP_M68040_SIGNAL_NEGATED,
  AP_M68040_SIGNAL_ASSERTED,
  AP_M68040_SIGNAL_FROM_MMU
} ap_m68040_signal_state_t;

typedef struct {
  const char *name;
  /* `TT1-TT0`. */
  unsigned transfer_type;
  /* True where Table 7-2 prints a literal address rather than "Access
   * Address"; `address` is then that literal. */
  bool address_is_fixed;
  uint32_t address;
  /* `UPA1-UPA0`: `$0`, or the page's U1 and U0 bits. */
  ap_m68040_signal_state_t upa;
  ap_m68040_signal_state_t ciout;
  /* Sizes this access can use, as a mask of `1u << ap_m68040_size_t`. */
  unsigned sizes;
  /* Note 2: "the TLNx signals are defined only for normal push accesses and
   * normal data line read accesses." */
  bool tln_defined;
  /* Note 3: `LOCK` "is asserted during TAS, CAS, and CAS2 operand accesses and
   * for some table search update sequences". */
  bool may_lock;
  bool read_only;
} ap_m68040_access_info_t;

[[nodiscard]] const ap_m68040_access_info_t *
ap_m68040_access(ap_m68040_access_t access);

/* ---------------------------------------------------------------------------
 * Terminations, Tables 7-4 and 7-5.
 * ------------------------------------------------------------------------- */

/* Table 7-5. The signals are given as asserted/negated, not as pin levels. */
typedef enum {
  AP_M68040_TERM_WAIT,      /* neither asserted: insert wait states */
  AP_M68040_TERM_NORMAL,    /* TA alone */
  AP_M68040_TERM_BUS_ERROR, /* TEA alone */
  AP_M68040_TERM_RETRY      /* both */
} ap_m68040_termination_t;

[[nodiscard]] ap_m68040_termination_t ap_m68040_terminate(bool ta, bool tea);

/* Table 7-4, which adds `AVEC` to the same two signals. */
typedef enum {
  AP_M68040_IACK_WAIT,
  AP_M68040_IACK_VECTOR,     /* latch the vector number on D7-D0 */
  AP_M68040_IACK_AUTOVECTOR, /* AVEC asserted with TA */
  AP_M68040_IACK_SPURIOUS,   /* TEA alone */
  AP_M68040_IACK_RETRY
} ap_m68040_iack_result_t;

[[nodiscard]] ap_m68040_iack_result_t ap_m68040_iack_terminate(bool ta,
                                                               bool tea,
                                                               bool avec);

/* "The vector number, which is the sum of the interrupt priority level plus 24
 * ($18)." Seven of them, for levels 1 through 7. */
#define AP_M68040_AUTOVECTOR_BASE 24u
/* "The M68040 automatically generates the spurious interrupt vector number 24
 * ($18) instead of the interrupt vector number" -- the same number the
 * autovector count starts from, so level 0 would collide if it existed. */
#define AP_M68040_SPURIOUS_VECTOR 24u

[[nodiscard]] unsigned ap_m68040_autovector(unsigned level);

/* ---------------------------------------------------------------------------
 * Bus arbitration, Table 7-6 -- keyed as §7.8.1 says, not as the table does.
 * ------------------------------------------------------------------------- */

typedef enum {
  AP_M68040_ARB_IDLE,
  AP_M68040_ARB_SNOOP,
  AP_M68040_ARB_IMPLICIT_OWNERSHIP,
  AP_M68040_ARB_ACTIVE_BUS_CYCLE,
  AP_M68040_ARB_PARK,
  AP_M68040_ARB_ALTERNATE_MASTER
} ap_m68040_arbitration_t;

/* `drives_bb` is whether the M68040 asserts `BB` rather than three-stating it;
 * `bus_driven` is whether it drives the address and data buses at all;
 * `defined_values` distinguishes an active cycle from park, which drives the
 * bus "with undefined values". `bg` is the arbiter's grant. `snooping` picks
 * the snoop state apart from idle -- the two differ only in readiness, "the
 * status of BB and the bus is identical". */
[[nodiscard]] ap_m68040_arbitration_t
ap_m68040_arbitration_state(bool bg, bool drives_bb, bool bus_driven,
                            bool defined_values, bool snooping);

/* ---------------------------------------------------------------------------
 * Stated constants.
 * ------------------------------------------------------------------------- */

/* §7.10. */
#define AP_M68040_RESET_MIN_ASSERT_CLOCKS 10u
#define AP_M68040_RESET_SYNCHRONISE_CLOCKS 2u
#define AP_M68040_RESET_INTERNAL_CLOCKS 128u
#define AP_M68040_RESET_INSTRUCTION_RSTO_CLOCKS 512u

/* §7.4.2 and §7.4.4: "a burst-inhibited line read completes in eight clocks
 * instead of the five required for a burst read", and the same for a write. */
#define AP_M68040_LINE_BURST_CLOCKS 5u
#define AP_M68040_LINE_BURST_INHIBITED_CLOCKS 8u

/* §7.11.1: "large buffers have a nominal output impedance of 6 ohms **for both
 * high and low drive** ... small buffers have a nominal impedance of 25 ohms
 * for high and low drive."
 *
 * **§11 does not agree about the large buffer.** Its worked example in §11.9
 * computes the low case as "(49.6 mA)^2 x 6 ohms" and the high case as
 * "(50.8 mA)^2 x **12** ohms", and Figure 11-8 labels the large buffer
 * "TYPICAL Z0 = 4-12 ohms" against the small buffer's flat 25. So §7.11.1's
 * single symmetric figure is the loosest of the three statements: the large
 * buffer is asymmetric, roughly 6 ohms pulling low and 12 pulling high, inside
 * a 4-12 ohm spread. The constants below keep §7.11.1's nominal values because
 * that is what §7 states and what a model of the *mode selection* needs; the
 * asymmetry is recorded here rather than in a number, since nothing in this
 * core computes a drive current. */
#define AP_M68040_LARGE_BUFFER_OHMS 6u
#define AP_M68040_SMALL_BUFFER_OHMS 25u
/* §11.9's high-drive figure for the large buffer, and Figure 11-8's range. */
#define AP_M68040_LARGE_BUFFER_HIGH_OHMS 12u
#define AP_M68040_LARGE_BUFFER_OHMS_MIN 4u
#define AP_M68040_LARGE_BUFFER_OHMS_MAX 12u

/* §11.5's frequency of operation: 20 MHz minimum at every speed grade, and
 * three grades for the MC68040. The MC68040V is the exception and §1.1 says so
 * -- it "operates down to 0 MHz", which is only a distinction because the base
 * part has a floor. */
#define AP_M68040_MIN_FREQUENCY_HZ 20000000u

/* Table 11-3 rates the MC68040 at 25 and 33 MHz and §11.5 adds 40; Table 11-4
 * rates the MC68LC040 and MC68EC040 at 20, 25 and 33 -- a different range,
 * reaching lower and stopping earlier. */
[[nodiscard]] bool ap_m68040_is_rated_frequency(unsigned hz, bool ec_or_lc);

/* §7.5.1: "an interrupt request that is held constant for two consecutive
 * clock periods is considered a valid input." */
#define AP_M68040_IPL_DEBOUNCE_CLOCKS 2u

/* Whether an access allocates a cache line on a miss. §7.4.1 and §7.4.3 list
 * the ones that do not: table searches and updates, exception vector fetches,
 * exception stacking, and the stack deallocation of an `RTE`. */
typedef enum {
  AP_M68040_NONALLOCATING_TABLE_SEARCH,
  AP_M68040_NONALLOCATING_TABLE_UPDATE,
  AP_M68040_NONALLOCATING_VECTOR_FETCH,
  AP_M68040_NONALLOCATING_EXCEPTION_STACKING,
  AP_M68040_NONALLOCATING_RTE_DEALLOCATION,
  AP_M68040_NONALLOCATING_COUNT
} ap_m68040_nonallocating_t;

[[nodiscard]] const char *
ap_m68040_nonallocating_name(ap_m68040_nonallocating_t which);

#endif /* APOLLO_CPU_M68040_AP_M68040_BUS_H */
