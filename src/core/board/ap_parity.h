/* The memory array's parity circuit: `008778-03` §3.3, and the self-test that
 * is the only thing in the machine which ever uses it.
 *
 * ## What the hardware is
 *
 * "The memory array consists of 36 RAM chips. Thirty-two of these are used for
 * data. The other four chips are used for the parity circuitry." And: "The
 * parity circuitry for the memory array uses four F280 parity
 * checker/generators to generate the parity bits on Write operations and to
 * check the parity bits on Read operations."
 *
 * Four checkers over thirty-two data bits is **one per byte lane**, which is
 * what makes the four-bit fields in the CPU status and control registers four
 * bits wide. Parity is *generated* on every write and *checked* on every read;
 * a program cannot see it at all unless something makes it disagree.
 *
 * One thing does: "The parity circuitry can be forced bad by inputting to the
 * F280 and writing into the parity RAM in diagnostic mode. This approach
 * provides sufficient coverage of the parity circuitry for diagnostics." That
 * is CPU control register bit 3, and the boot PROM's self-test 7 is the
 * diagnostic it is for.
 *
 * ## What a parity error does
 *
 * `008778-03` §3.2: "The parity error interrupt is a non-maskable interrupt to
 * the CPU. It generates a **Level 7** interrupt to the CPU. When the vector is
 * fetched, it comes from the Level 7 **autovector** location in the CPU
 * exception table (0 x 07c)."
 *
 * Which settles three separate things a model would otherwise have to guess:
 * the level, that the cycle is autovectored rather than answered by one of the
 * 8259s like every other interrupt on this board, and the vector number. The
 * firmware corroborates all three -- self-test 7 installs its handler at `$7c`
 * off a VBR of `01000400` and expects to arrive there.
 *
 * The same section says how it ends: "The interrupt handler checks the status
 * register to detect which one of these conditions exists. Writing to the
 * status register clears the interrupt status." So the request is a **level**
 * held up by the status register's parity bits, not an event -- which is why
 * this module keeps no pending flag of its own. It is asserted exactly while
 * those bits are set and the control register's interrupt enable is on, and
 * `clr.w $10000` is what drops it. `019411-A00`'s Clear Parity Error Flag at
 * `016406` drops it too, by the same route, without being wired for separately.
 *
 * ## Why one bit per byte of main memory, and not a list
 *
 * The state is the parity RAM: nine bits stored per byte, of which eight are
 * the data, so what has to be remembered is one bit per byte -- "the parity
 * stored for this byte disagrees with the data". Writing a byte normally makes
 * them agree again, which is the whole of the clearing behaviour and needs no
 * separate rule.
 *
 * The oracle instead tracks a **single** bad longword in two file-scope
 * variables and installs a MAME read handler over it, capped at forty
 * installations with a comment explaining that the memory system runs out of
 * handlers; a second forced write while one is outstanding loses the first, and
 * the handler is never really uninstalled. That is enough for the one self-test
 * that exists and it is not the hardware. A bitmap is `ram_bytes / 8` and costs
 * nothing to be exact with, so this core is exact.
 *
 * ## What is not settled: which lane bit is which byte
 *
 * `PROVISIONAL`. Every boot PROM in hand drives the four lane bits **together**
 * -- `08` on this family and `F8` on the DN3000's, both meaning all four -- so
 * the field is only ever all or nothing and no image distinguishes bit 4 from
 * bit 7. Neither manual here lays the register out.
 *
 * So the assignment below is a convention, not a measurement: lane `n` is
 * address bit 0-1 of the byte, counted from the **most significant** byte of
 * the longword because the bus is big-endian, and it takes status bit `4 + n`.
 * Nothing in the machine can currently tell it from the other three
 * assignments. Closing it needs either the architecture handbook's register
 * layout or a program that forces one lane bad -- and no firmware here is that
 * program. Recorded in `docs/PROJECT_STATUS.md` as a named open item.
 */

#ifndef APOLLO_BOARD_AP_PARITY_H
#define APOLLO_BOARD_AP_PARITY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  /* One bit per byte of main memory: set means the parity RAM holds the wrong
   * parity for that byte, so a read of it fails the check.
   *
   * NULL until a caller attaches storage, and a board with none has **no
   * parity RAM fitted**: writes generate no parity to disagree with and reads
   * can never fail. That is a real machine -- and a visible one, because
   * `forced_writes` still counts the diagnostic writes that had nowhere to go,
   * so a run says "this board was asked to force bad parity 4 times and
   * cannot" rather than quietly passing the self-test. */
  uint8_t *bad;
  uint32_t bad_bytes;
  uint32_t ram_bytes;
  /* Diagnostics, not machine state: how many byte writes were made under a
   * control register forcing bad parity, and how many reads found one. */
  unsigned forced_writes;
  unsigned unstorable_writes;
  unsigned errors;
  /* The first byte offset whose check failed, which is the one worth having:
   * a run that raises an unexpected parity error needs to say *where*, and the
   * hundredth is always in the handler. */
  uint32_t first_error_offset;
} ap_parity_t;

void ap_parity_init(ap_parity_t *parity);

/* Fit the parity RAM. `bytes` must be at least `(ram_bytes + 7) / 8`; a smaller
 * store is refused rather than silently covering part of memory, because parity
 * that stops partway through the array describes no memory board. */
[[nodiscard]] bool ap_parity_attach(ap_parity_t *parity, uint8_t *bad,
                                    uint32_t bytes, uint32_t ram_bytes);

/* The status-register bit for the byte at this offset into main memory. See the
 * header on why the lane assignment is `PROVISIONAL`. */
[[nodiscard]] uint16_t ap_parity_lane_bit(uint32_t offset);

/* A byte has been written to main memory. `lanes` is the four-bit field from
 * `ap_boardreg_forced_lanes` -- zero for an ordinary write, which regenerates
 * correct parity, and that is how a bad byte is cleared. */
void ap_parity_write(ap_parity_t *parity, uint32_t offset, uint16_t lanes);

/* Whether a read of this byte fails its parity check. */
[[nodiscard]] bool ap_parity_check(ap_parity_t *parity, uint32_t offset);

#endif /* APOLLO_BOARD_AP_PARITY_H */
