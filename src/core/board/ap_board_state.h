/* Core board state hashing: the board's contribution to the whole-machine hash.
 *
 * `cpu/m68030/ap_m68030_state.h` is the processor's half and states the rule
 * both halves obey: a field that changes while the hash does not is the one
 * failure this cannot have, because the identity harness would then call two
 * diverging machines the same machine and every optimisation checked under it
 * would be checked against nothing. This is the same job for everything on the
 * other side of the bus — the board registers, the translation map, both
 * interrupt controllers, the interval timer, the calendar, both DMA
 * controllers, both serial ports, the node ID, the disk and tape controllers,
 * the graphics memories and the keyboard.
 *
 * ## What is state and what is instrumentation
 *
 * `ap_board_t` carries two very different kinds of field. The devices are the
 * machine; the counters beside them — `unmapped_reads`, `region_writes`, the
 * per-register serial tallies — are *our record of watching it*. Only the first
 * kind is hashed, and the line matters in both directions:
 *
 *  - A counter in the hash would make adding an instrument change every golden
 *    without any emulated behaviour changing, and would make two machines that
 *    behave identically compare unequal because one was watched more closely.
 *    An identity harness that rejects identical machines cannot be used at all.
 *  - Nothing is lost by leaving them out. They are *reported* beside the hash
 *    (`ap_machine_state`, and the headless frontend's boot summary), and
 *    `tests/goldens/probes.txt` already pins the bus-error count as its own
 *    column. A divergence in what was counted is caught there, as a number a
 *    reader can act on, rather than as a hash that merely differs.
 *
 * ## Main memory is not hashed here
 *
 * The board and the machine share one RAM buffer, and hashing several megabytes
 * twice would buy nothing. Memory contents belong to `ap_machine_hash`, which
 * has hashed them since before there was a board. What is hashed here is the
 * *extent*: a board with 8 Mbyte fitted is a different machine from one with 16,
 * even when every byte in common agrees.
 *
 * The graphics memories are the opposite case and *are* hashed in full. Nothing
 * else covers them: they are attached to the display controller rather than to
 * the machine, and they are the machine's output — a run that drew a different
 * picture is a different run.
 *
 * ## Read-only images
 *
 * The boot PROM is hashed in full. Which firmware is running is the largest
 * single fact about a boot, and the region is 64 Kbyte at most.
 *
 * The tape cartridge is hashed **by a digest taken once when the image is
 * opened**, which is the closing route this paragraph used to name as future
 * work. A `.ct` image is up to a hundred megabytes and re-reading it on every
 * hash would cost more than the run it is measuring; reading it once at load
 * costs one pass and nothing afterwards. `ap_ct_write_block` keeps the digest
 * current, so a cartridge loaded writable is described as it now stands rather
 * than as it arrived.
 *
 * The residual it closes: two different cartridges of exactly equal size used
 * to hash alike until one of them was read. Everything a run can change --
 * which block is buffered, where the head is, what the drive was told -- was
 * always hashed in full and still is.
 */

#ifndef APOLLO_BOARD_AP_BOARD_STATE_H
#define APOLLO_BOARD_AP_BOARD_STATE_H

#include "board/ap_board.h"
#include "state/ap_hash.h"

/* Each device's own contribution, exposed individually so a suite can perturb
 * one part and demand the hash moves without building a whole board around it,
 * and so the order below is visible rather than buried in one long function. */
void ap_board_hash_registers(ap_hash_t *st, const ap_boardreg_t *registers);
void ap_board_hash_translation_map(ap_hash_t *st, const ap_atmap_t *map);
void ap_board_hash_interrupts(ap_hash_t *st, const ap_intr_t *interrupts);
void ap_board_hash_timer(ap_hash_t *st, const ap_timer_t *timer);
void ap_board_hash_calendar(ap_hash_t *st, const ap_calendar_t *calendar);
void ap_board_hash_dma(ap_hash_t *st, const ap_dma_t *dma);
void ap_board_hash_sio(ap_hash_t *st, const ap_sio_t *sio);
void ap_board_hash_node_id(ap_hash_t *st, const ap_nodeid_t *node_id);
void ap_board_hash_disk(ap_hash_t *st, const ap_disk_t *disk);
void ap_board_hash_tape(ap_hash_t *st, const ap_tape_t *tape);
void ap_board_hash_graphics(ap_hash_t *st, const ap_graphics_t *graphics);
void ap_board_hash_ring(ap_hash_t *st, const ap_ring_ctl_t *ring);
void ap_board_hash_keyboard(ap_hash_t *st, const ap_kbd_t *keyboard);

/* The whole board: every device above, in a fixed order, plus the boot PROM and
 * main memory's extent. Not main memory's contents, and not the diagnostic
 * counters — see the header comment for both. */
void ap_board_hash(ap_hash_t *st, const ap_board_t *board);

/* The board on its own as one number, for a test or a caller that has a board
 * and no machine. A whole machine reports `ap_machine_state` instead. */
[[nodiscard]] uint64_t ap_board_state_hash(const ap_board_t *board);

#endif /* APOLLO_BOARD_AP_BOARD_STATE_H */
