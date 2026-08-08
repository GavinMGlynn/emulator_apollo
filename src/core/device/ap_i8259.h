/* Intel 8259A Programmable Interrupt Controller.
 *
 * `[8259]` *8259A Programmable Interrupt Controller*, Intel, order number
 * 231468-003, December 1988. Every figure and rule below cites it.
 *
 * The Apollo core board carries two, cascaded: `008778-03` §2.4.8, "Interrupts
 * are implemented using two cascaded Intel 8259A Programmable Interrupt
 * Controller (PIC) chips." This module is the *part*, with no Apollo in it —
 * the pairing, the addresses and the machine's priority table belong to the
 * board and are a separate module. A part that knew which machine it was in
 * could not be checked against the datasheet.
 *
 * ## Where the figures went, and how they came back
 *
 * Figure 8, the Operation Command Word format, did not survive the scan: the
 * page carries the three OCW bit-position rows and then three bare figure
 * references where the command tables should be. The OCW2 command encodings
 * are therefore taken from the *prose*, which states seven of the eight
 * outright:
 *
 *   non-specific EOI          "OCW2 (EOI = 1, SL = 0, R = 0)"
 *   specific EOI              "(EOI = 1, SL = 1, R = 0, and L0-L2 is the
 *                              binary level of the IS bit to be reset)"
 *   rotate on non-specific    "the Rotation on Non-Specific EOI Command
 *   EOI                        (R = 1, SL = 0, EOI = 1)"
 *   rotate in automatic EOI   "set by (R = 1, SL = 0, EOI = 0) and cleared by
 *                              (R = 0, SL = 0, EOI = 0)"
 *   set priority              "The Set Priority command is issued in OCW2
 *                              where: R = 1, SL = 1, L0-L2 is the binary
 *                              priority level code of the bottom priority
 *                              device"
 *   rotate on specific EOI    "(R = 1, SL = 1, EOI = 1 and L0-L2 = IR level to
 *                              receive bottom priority)"
 *
 * That leaves exactly one combination unaccounted for, `R=0 SL=1 EOI=0`, which
 * is therefore the no-operation by elimination rather than by assertion. It is
 * marked where it is handled. This is the same recovery route as the 68030's
 * Figure 7-61: prose describing a lost figure is still the manual.
 *
 * ## What is modelled and what is refused
 *
 * MCS-80/85 mode (`uPM = 0`) is **refused**, not approximated. In that mode the
 * part answers an acknowledge with a three-byte `CALL` sequence and an address
 * assembled from ICW1's `ADI` interval — a different protocol on a different
 * number of bus cycles. This machine never uses it: `008778-03` §3.2's vector
 * byte table is `T7 T6 T5 T4 T3` followed by the three level bits, which is
 * precisely what `[8259]` describes for 8086 mode — "A15-A11 are inserted in the
 * five most significant bits of the vectoring byte and the 8259A sets the three
 * least significant bits according to the interrupt level. A10-A5 are ignored".
 * Two independent manuals describing the same byte is a strong check on the
 * transcription; implementing a mode neither of them exercises would be
 * untested code pretending to be coverage.
 */

#ifndef APOLLO_DEVICE_AP_I8259_H
#define APOLLO_DEVICE_AP_I8259_H

#include <stdbool.h>
#include <stdint.h>

#define AP_I8259_LEVELS 8u

/* Where the initialization sequence has got to. `[8259]` Figure 6: ICW1 starts
 * it, ICW2 always follows, ICW3 only if `SNGL = 0`, ICW4 only if `IC4 = 1`. */
typedef enum {
  AP_I8259_INIT_READY = 0, /* initialised; writes are OCWs */
  AP_I8259_INIT_ICW2,
  AP_I8259_INIT_ICW3,
  AP_I8259_INIT_ICW4,
} ap_i8259_init_t;

typedef struct {
  /* `[8259]`'s three registers. */
  uint8_t irr; /* interrupt request: levels asking to be serviced */
  uint8_t isr; /* in service */
  uint8_t imr; /* mask; 1 inhibits */

  /* The IR pins as driven, kept apart from `irr` because in edge mode they are
   * not the same thing: a level that is still high has no further edges to
   * give. */
  uint8_t pins;

  /* Which level currently holds highest priority. Zero after initialization --
   * "After the initialization sequence, IR0 has the highest priority and IR7
   * the lowest" -- and moved by the rotate commands. */
  uint8_t highest_priority;

  /* ICW2's five significant bits, already shifted into place. */
  uint8_t vector_base;

  /* ICW3. Master: a bit per cascaded slave. Slave: its own ID in bits 2-0. */
  uint8_t cascade;

  /* ICW1 */
  bool single;          /* SNGL: the only 8259A in the system */
  bool level_triggered; /* LTIM */

  /* ICW4 */
  bool auto_eoi;             /* AEOI */
  bool x86_mode;             /* uPM */
  bool buffered;             /* BUF */
  bool master;               /* M/S, meaningful only when buffered */
  bool special_fully_nested; /* SFNM */

  /* OCW2/OCW3 state */
  bool auto_rotate;  /* rotate in automatic EOI mode */
  bool special_mask; /* special mask mode */
  bool read_isr;     /* a status read returns ISR rather than IRR */
  bool poll_pending; /* the next read is an acknowledge, not a status read */

  ap_i8259_init_t init_state;

  /* ICW1's IC4 bit, which decides whether ICW4 is expected -- and has to
   * survive ICW2 and ICW3, neither of which carries it.
   *
   * Per-instance and not a file static, which is not a stylistic point: this
   * part is cascaded in pairs, and a shared one would let the master's
   * initialization decide whether the slave expects an ICW4. */
  bool expect_icw4;

  /* True between the two acknowledge cycles. `[8259]` runs the 8086 sequence as
   * two INTA pulses and the part behaves differently in each. */
  bool acknowledging;
  uint8_t acknowledged_level;
} ap_i8259_t;

/* Power-on. Not the same as ICW1: an uninitialised 8259A has no vector base and
 * no mode, and this core will not invent one. Until ICW1 arrives the part holds
 * no requests and asserts no interrupt. */
void ap_i8259_init(ap_i8259_t *pic);

/* Register access. `a0` is the address line the part uses to tell its two
 * registers apart -- `[8259]`: "This line can be tied directly to one of the
 * address lines." */
void ap_i8259_write(ap_i8259_t *pic, bool a0, uint8_t value);
[[nodiscard]] uint8_t ap_i8259_read(ap_i8259_t *pic, bool a0);

/* Drive one IR pin. Edge or level triggered per ICW1's LTIM, which this handles
 * internally; a caller drives the wire and nothing else. */
/* Drive one request line, reporting whether the wire moved.
 *
 * The return exists for `ap_intr`, whose board re-drives every device's line on
 * every emulated instruction: almost all of those writes change nothing, and
 * the cascade recalculation each one forced was about a fifth of a boot. Not
 * `[[nodiscard]]` -- most callers are simply setting a line. */
bool ap_i8259_set_request(ap_i8259_t *pic, unsigned line, bool asserted);

/* The INT output: whether a request is pending that priority and mask allow to
 * be serviced. */
[[nodiscard]] bool ap_i8259_interrupt_pending(const ap_i8259_t *pic);

/* The level that would be acknowledged, or -1. Does not change state, so a
 * board can ask the master which slave it must forward an acknowledge to
 * without committing to it. */
[[nodiscard]] int ap_i8259_poll_level(const ap_i8259_t *pic);

/* First acknowledge cycle: freeze, set the ISR bit, clear the IRR bit. Answers
 * the level acknowledged.
 *
 * Never fails. `[8259]`: "If no interrupt request is present at step 4 of
 * either sequence (i.e., the request was too short in duration) the 8259A will
 * issue an interrupt level 7. Both the vectoring bytes and the CAS lines will
 * look like an interrupt level 7 was requested." So a spurious acknowledge
 * produces level 7 rather than an error, and a board cannot tell it from a real
 * one -- which is the hardware's behaviour and the reason spurious interrupts
 * are a thing that has to be handled in software. */
unsigned ap_i8259_acknowledge_first(ap_i8259_t *pic);

/* Second acknowledge cycle: the vector byte. In AEOI the ISR bit is cleared
 * here -- "at the trailing edge of the last interrupt acknowledge pulse
 * (third pulse in MCS-80/85, second in 8086)". */
[[nodiscard]] uint8_t ap_i8259_acknowledge_second(ap_i8259_t *pic);

/* Whether this part was programmed for 8086-style vectoring. False means
 * MCS-80/85, which this module refuses; see the header. A board must check
 * rather than assume, because a refused mode returning a plausible byte is
 * exactly the failure that hides. */
[[nodiscard]] bool ap_i8259_vectoring_supported(const ap_i8259_t *pic);

#endif /* APOLLO_DEVICE_AP_I8259_H */
