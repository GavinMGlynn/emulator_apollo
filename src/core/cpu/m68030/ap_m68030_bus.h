/* MC68030 bus cycle state machine.
 *
 * This is the bottom of the timing stack, and it is deliberately the first
 * thing built in the CPU phase. `docs/references/M68030_TIMING.md` records why:
 * Motorola's published instruction cycle counts are averages over prefetch
 * alignment, taken with assumed two-clock bus cycles and assumed cache states,
 * so no published number is a value any single execution actually takes. A core
 * that adds up table entries reproduces an average the hardware never exhibits.
 * The way out is to make timing *emergent* -- and emergent timing means every
 * clock an instruction takes is a clock some real bus cycle or internal
 * operation took. That starts here.
 *
 * ## States, and why they are half-clocks
 *
 * `[030]` ch. 7 describes a bus cycle in states S0..S5, each **one-half clock**
 * ("One-half clock later in state 1 (S1)...", 7.3.1 p. 7-31). So a minimum
 * asynchronous cycle is six states -- three clocks.
 *
 * The project's rule is one `tick()` per machine cycle, and that is preserved:
 * `ap_m68030_bus_tick()` advances one *clock* and internally runs the two
 * states that clock contains, in order. The states are modelled rather than
 * collapsed because the manual specifies the cycle in them, and a translation
 * layer between the manual's granularity and ours is exactly where an off-by-a-
 * half-clock hides. Half-clocks are exactly representable for every CPU in this
 * machine -- 12, 20, 25 and 33 MHz all have even periods in AP_TIME_BASE_HZ
 * units (550, 330, 264, 200) -- so this costs no precision. `bus_suite` asserts
 * that evenness rather than assuming it.
 *
 * ## The documented sequence (`[030]` 7.3.1, pp. 7-31 ff.)
 *
 *   S0  ECS asserted (one-half clock; OCS too, if this is the first external
 *       cycle of an operand operation). Address, FC0-FC2, R/W, SIZ0-SIZ1 and
 *       CIOUT become valid.
 *   S1  AS asserted; DS asserted on a read. ECS/OCS negated.
 *   S2  DBEN asserted, enabling external data buffers.
 *   S3  Termination is sampled. If DSACKx is not recognised by the *start* of
 *       S3, the processor inserts wait states instead of proceeding to S4/S5.
 *   S4  CIIN sampled at the beginning; data latched at the end.
 *   S5  AS, DS and DBEN negated. Address, R/W, SIZ and FC held valid through
 *       S5 to give memory its address hold time.
 *
 * ## Two termination paths
 *
 * - **Asynchronous**, terminated by DSACKx: three clocks minimum (7.3.1).
 * - **Synchronous**, terminated by STERM: "a two-clock (minimum) bus cycle for
 *   32-bit ports and single-clock (minimum) burst accesses, although wait
 *   states can be inserted for these cycles as well" (7.3.4, p. 7-48). Only
 *   32-bit ports may assert STERM.
 *
 * A wait state is a whole clock, inserted before the cycle advances to S4.
 *
 * Nothing here decides *which* termination a given address gets, or how long it
 * takes to arrive. That is the memory system's business, and keeping it out of
 * this file is what lets bus contention be emergent rather than tabulated.
 */

#ifndef APOLLO_CPU_M68030_AP_M68030_BUS_H
#define APOLLO_CPU_M68030_AP_M68030_BUS_H

#include <stdbool.h>
#include <stdint.h>

/* Bus cycle states, `[030]` 7.3.1. Each is one-half clock. S_IDLE is not a
 * documented state: it is the machine between cycles. */
typedef enum {
  AP_M68030_S_IDLE = 0,
  AP_M68030_S0,
  AP_M68030_S1,
  AP_M68030_S2,
  AP_M68030_S3,
  AP_M68030_S4,
  AP_M68030_S5,
} ap_m68030_bus_state_t;

/* Transfer size, driven on SIZ0-SIZ1. The 68030 also requests three-byte
 * transfers for some operations (`[030]` 7.3.1), which is why this is not
 * simply 1/2/4. */
typedef enum {
  AP_M68030_SIZE_BYTE = 1,
  AP_M68030_SIZE_WORD = 2,
  AP_M68030_SIZE_LONG = 4,
  AP_M68030_SIZE_THREE = 3,
} ap_m68030_size_t;

/* How the addressed device ended the cycle. AP_M68030_TERM_NONE means it has
 * not answered yet, which is what causes wait states. */
typedef enum {
  AP_M68030_TERM_NONE = 0,
  AP_M68030_TERM_DSACK, /* asynchronous; three-clock minimum */
  AP_M68030_TERM_STERM, /* synchronous, 32-bit port only; two-clock minimum */
  AP_M68030_TERM_BERR,  /* bus error */
} ap_m68030_term_t;

typedef struct {
  /* The request, valid from S0. */
  uint32_t address;
  uint8_t function_code; /* FC0-FC2 */
  ap_m68030_size_t size;
  bool read; /* R/W: true = read */

  /* State. */
  ap_m68030_bus_state_t state;
  ap_m68030_term_t termination;
  uint32_t clocks;      /* clocks elapsed in the current cycle */
  uint32_t wait_states; /* wait clocks inserted so far */
  bool active;
  bool complete;
  /* Set once a DSACK termination has been accepted at S3, meaning the next
   * clock runs S4/S5. It is a separate field rather than a state value because
   * `state` is the *documented* half-clock state and must stay that -- reusing
   * it to also mean "which clock comes next" is what made S3 loop back into
   * S2/S3 forever the first time this was written. */
  bool advancing_to_s4;

  /* Signals, as of the end of the last tick. Named as the manual names them,
   * asserted-true here rather than active-low, because inverting at the pin is
   * the memory system's problem and not this model's. */
  /* Burst mode, `[030]` §7.3.7. A burst fills a whole cache line in one cycle
   * that stays open across up to four long words. */
  bool cbreq;             /* CBREQ: this cycle requests a burst */
  bool cback;             /* CBACK: the device says it can supply another */
  bool bursting;          /* the burst was accepted and is under way */
  unsigned burst_beats;   /* long words transferred, including the first */

  bool ecs;  /* external cycle start */
  bool ocs;  /* operand cycle start */
  bool as;   /* address strobe */
  bool ds;   /* data strobe */
  bool dben; /* data buffer enable */
} ap_m68030_bus_t;

/* Begin a bus cycle. The machine enters S0 on the next tick. `first_operand`
 * drives OCS, which the manual asserts only on the first external cycle of an
 * operand operation. */
void ap_m68030_bus_begin(ap_m68030_bus_t *bus, uint32_t address,
                         uint8_t function_code, ap_m68030_size_t size, bool read,
                         bool first_operand);

/* The number of long words a full burst transfers: "The MC68030 allows a burst
 * of as many as four long words." */
#define AP_M68030_BURST_BEATS 4

/* Assert CBREQ on the cycle about to run, which `ap_m68030_cache_burst_request`
 * decides. Call between `begin` and the first `tick`. */
void ap_m68030_bus_request_burst(ap_m68030_bus_t *bus);

/* The device's CBACK answer. "burst mode is only initiated if both of these
 * signals are asserted for a synchronous cycle" -- so a burst needs CBREQ, CBACK
 * *and* STERM, and any one of them missing leaves an ordinary single cycle.
 *
 * "CBACK ... can be asserted independently of the CBREQ signal", so a device
 * volunteering it without a request changes nothing. */
void ap_m68030_bus_acknowledge_burst(ap_m68030_bus_t *bus, bool acknowledged);

/* Offer a termination to the cycle in progress. The memory system calls this
 * when it is ready to answer; whether it is early enough to avoid a wait state
 * is decided by when it arrives relative to S3, exactly as on the real part. */
void ap_m68030_bus_terminate(ap_m68030_bus_t *bus, ap_m68030_term_t term);

/* Advance one clock -- both of that clock's states, in order. Returns true when
 * the cycle completed during this tick. */
bool ap_m68030_bus_tick(ap_m68030_bus_t *bus);

/* True while a cycle is in progress. */
[[nodiscard]] bool ap_m68030_bus_active(const ap_m68030_bus_t *bus);

#endif /* APOLLO_CPU_M68030_AP_M68030_BUS_H */
