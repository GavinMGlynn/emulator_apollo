/* Intel 8254 programmable interval timer.
 *
 * `[8254]` *1983 Intel Microprocessors and Peripherals Handbook*, the 8254
 * chapter beginning at p. 6-150. Fetched for this module: the project held the
 * 8237A and 8259A datasheets and no 8254, and `RING.md` finding 41 identifies
 * two of these on the ring controller.
 *
 * ## Why this part, and how it was identified
 *
 * The ring controller's `+800`-`+806` and `+C00`-`+C06` were read for a long
 * time as the dual-ported RAM buffer, because the firmware takes their
 * addresses with `lea.l` rather than their contents. `RING.md` finding 41
 * settles them as two 8254s on four independent points, and finding 41a records
 * the correction: the `lea` is there because the LSB-then-MSB access helper
 * takes a pointer.
 *
 * The identification is worth repeating here because it is also this module's
 * acceptance test. The ring firmware writes control words `$30`, `$70` and
 * `$B0` -- counters 0, 1 and 2, each "LSB then MSB", each mode 0 -- then loads
 * counters through a helper that writes LSB then MSB, matching that field. It
 * writes `$E4`, which decodes as read-back, latch **status**, counter 1. And it
 * tests bit 14 of the word it reads back, which is the status byte's NULL COUNT
 * in the upper half. `$E4` exists **only on the 8254**: the 8253 has no
 * read-back command. So the part is pinned by the firmware's own use of a
 * command the earlier part does not have.
 *
 * ## What is modelled
 *
 * The programming model: the control word's four fields, the counter latch
 * command, the read-back command in both of its halves, the LSB/MSB access
 * sequencing on both reads and writes, the NULL COUNT flag with the exact
 * transitions Figure 12 gives, and counting with the OUT pin.
 *
 * Modes 0 and 2 and 3 are implemented in full. Modes 1, 4 and 5 are decoded and
 * counted, and their OUT waveform is mode 0's -- they are gate-triggered or
 * strobe outputs whose distinguishing behaviour needs a GATE edge, and this
 * board drives no gate. That is a *board* fact rather than a part fact and is
 * marked as such: `ap_i8254_mode_gated` reports which modes need the pin this
 * board does not wire, so a caller is told rather than silently given mode 0's
 * shape.
 *
 * ## Two figures that decide the whole part
 *
 * Figure 11, the status byte:
 *
 *     D7        OUT pin state -- 1 = OUT is 1
 *     D6        NULL COUNT -- 1 = null count, 0 = count available for reading
 *     D5-D0     RW1 RW0 M2 M1 M0 BCD, "exactly as written in the last Mode
 *               Control Word"
 *
 * Figure 12, NULL COUNT, which is the one a driver polls:
 *
 *     write to the control word register   -> NULL COUNT = 1 (that counter only)
 *     write to the count register          -> NULL COUNT = 1
 *     new count loaded into CE (CR -> CE)  -> NULL COUNT = 0
 *
 * with the footnote that for a two-byte count it "goes to 1 when the second
 * byte is written". A model that set it on the first byte would clear it a byte
 * early and a driver waiting for the count to take would proceed on a counter
 * still holding the old one.
 */

#ifndef APOLLO_DEVICE_AP_I8254_H
#define APOLLO_DEVICE_AP_I8254_H

#include <stdbool.h>
#include <stdint.h>

#define AP_I8254_COUNTERS 3u

/* The four register addresses: three counters and the control word. */
typedef enum {
  AP_I8254_COUNTER_0 = 0u,
  AP_I8254_COUNTER_1 = 1u,
  AP_I8254_COUNTER_2 = 2u,
  AP_I8254_CONTROL = 3u,
} ap_i8254_reg_t;

/* Control word fields, Figure 7. */
#define AP_I8254_CW_SC 0xC0u  /* select counter, or 11 for read-back */
#define AP_I8254_CW_RW 0x30u  /* 00 latch, 01 LSB, 10 MSB, 11 LSB then MSB */
#define AP_I8254_CW_MODE 0x0Eu
#define AP_I8254_CW_BCD 0x01u

/* Read-back command, Figure 10: `11` in the counter-select field, then two
 * active-low selectors and a bit per counter. */
#define AP_I8254_READ_BACK 0xC0u
#define AP_I8254_RB_NOT_COUNT 0x20u  /* D5: 0 latches the count */
#define AP_I8254_RB_NOT_STATUS 0x10u /* D4: 0 latches the status */
#define AP_I8254_RB_COUNTER_2 0x08u
#define AP_I8254_RB_COUNTER_1 0x04u
#define AP_I8254_RB_COUNTER_0 0x02u

/* Status byte, Figure 11. */
#define AP_I8254_STATUS_OUT 0x80u
#define AP_I8254_STATUS_NULL_COUNT 0x40u

typedef enum {
  AP_I8254_RW_LATCH = 0u,
  AP_I8254_RW_LSB = 1u,
  AP_I8254_RW_MSB = 2u,
  AP_I8254_RW_LSB_THEN_MSB = 3u,
} ap_i8254_rw_t;

typedef struct {
  /* "The counter's programmed Mode exactly as written in the last Mode Control
   * Word" -- kept whole rather than as decoded fields, because that is what the
   * status byte's low six bits must return. */
  uint8_t control;

  uint16_t counter;  /* CE, the counting element */
  uint16_t latch;    /* CR, the count register a write loads */
  bool gate;         /* the GATE pin; high enables counting in modes 0, 2, 3 */
  bool out;          /* the OUT pin */
  bool null_count;   /* status D6 */
  bool counting;     /* a count has been loaded and not yet run out */

  /* The output latch and its own valid flag. "This count is held in the latch
   * until it is read by the CPU ... The count is then unlatched automatically
   * and the OL returns to following the counting element." */
  uint16_t count_latch;
  bool count_latched;
  uint8_t status_latch;
  bool status_latched;

  /* Which half a two-byte access is on. Separate for reads and writes: a
   * driver may interleave a latched read with a write, and one sequence must
   * not move the other's cursor. */
  bool write_msb_next;
  bool read_msb_next;
} ap_i8254_counter_t;

typedef struct {
  ap_i8254_counter_t counter[AP_I8254_COUNTERS];
} ap_i8254_t;

void ap_i8254_reset(ap_i8254_t *pit);

/* A register write: three counters and the control word. */
void ap_i8254_write(ap_i8254_t *pit, ap_i8254_reg_t reg, uint8_t value);

/* A register read. The control address is not readable -- "A1,A0 = 11" selects
 * the control word register for *writes*, and the part drives nothing for a
 * read there. */
[[nodiscard]] uint8_t ap_i8254_read(ap_i8254_t *pit, ap_i8254_reg_t reg);

/* One CLK pulse to every counter that is running. The board decides the rate. */
void ap_i8254_clock(ap_i8254_t *pit);

/* Drive one counter's GATE pin. */
void ap_i8254_set_gate(ap_i8254_t *pit, unsigned index, bool high);

/* The OUT pin. */
[[nodiscard]] bool ap_i8254_out(const ap_i8254_t *pit, unsigned index);

/* Whether a counter's programmed mode is one whose distinguishing behaviour
 * needs a GATE edge this board does not drive -- modes 1, 4 and 5. Reported so
 * that a caller is told rather than silently handed mode 0's waveform. */
[[nodiscard]] bool ap_i8254_mode_gated(const ap_i8254_t *pit, unsigned index);

/* The programmed mode, `M2 M1 M0`, as a number 0-5. Modes 6 and 7 alias to 2
 * and 3, which Figure 7 states outright. */
[[nodiscard]] unsigned ap_i8254_mode(const ap_i8254_t *pit, unsigned index);

#endif /* APOLLO_DEVICE_AP_I8254_H */
