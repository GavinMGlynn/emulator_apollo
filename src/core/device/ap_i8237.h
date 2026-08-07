/* Intel 8237A Multimode DMA Controller.
 *
 * `[8237]` *8237A High Performance Programmable DMA Controller*, Intel, order
 * number 231466. The Apollo board carries two, `008778-03` Table 2-8 placing
 * them at `010C00` and `010D00`.
 *
 * This is the part. The board wiring -- which controller is at which address and
 * at what stride -- belongs to `board/`, and was measured rather than assumed:
 * `FINDINGS.md` C13.
 *
 * ## What is modelled: the programming model, entire
 *
 * All sixteen register addresses of `[8237]` Figure 6, the four channels with
 * their base and current address and word-count registers, the shared
 * first/last flip-flop, and the command, mode, request, mask, status and
 * temporary registers. Autoinitialise reload and the mask-on-terminal-count
 * rule are here too, because both are properties of the registers rather than
 * of a transfer.
 *
 * ## What is not: the transfers themselves
 *
 * A DMA transfer is a bus cycle run by something other than the processor, and
 * this machine has no shared arbitration point yet -- it is Phase 3's first
 * item, and it has been waiting since the start of the phase for a second bus
 * master to exist. This module is that second master's registers; it cannot
 * move a byte until there is a bus to arbitrate for.
 *
 * That is a real dependency and not a scoping convenience, and it cuts cleanly:
 * every register here is programmable and observable by software without a
 * single transfer occurring, which is exactly what the boot PROM does to a
 * controller it has not yet used. `ap_i8237_service_pending` reports when a
 * channel is asking, which is the hand-off point to the arbiter when it exists.
 */

#ifndef APOLLO_DEVICE_AP_I8237_H
#define APOLLO_DEVICE_AP_I8237_H

#include <stdbool.h>
#include <stdint.h>

#define AP_I8237_CHANNELS 4u
#define AP_I8237_REGISTERS 16u

/* `[8237]` Figure 6, the software command codes. Registers 0-7 are the four
 * channels' address and word count, two apiece. */
typedef enum {
  AP_I8237_REG_STATUS_COMMAND = 8u,  /* read status, write command */
  AP_I8237_REG_REQUEST = 9u,         /* write only */
  AP_I8237_REG_MASK_SINGLE = 10u,    /* write only */
  AP_I8237_REG_MODE = 11u,           /* write only */
  AP_I8237_REG_CLEAR_FLIPFLOP = 12u, /* write only */
  AP_I8237_REG_TEMP_MASTERCLEAR = 13u, /* read temporary, write master clear */
  AP_I8237_REG_CLEAR_MASK = 14u,     /* write only */
  AP_I8237_REG_MASK_ALL = 15u,       /* write only */
} ap_i8237_reg_t;

/* Command register, `[8237]` Figure 5. */
#define AP_I8237_CMD_MEM_TO_MEM 0x01u
#define AP_I8237_CMD_CH0_ADDRESS_HOLD 0x02u
#define AP_I8237_CMD_CONTROLLER_DISABLE 0x04u
#define AP_I8237_CMD_COMPRESSED_TIMING 0x08u
#define AP_I8237_CMD_ROTATING_PRIORITY 0x10u
#define AP_I8237_CMD_EXTENDED_WRITE 0x20u
#define AP_I8237_CMD_DREQ_ACTIVE_LOW 0x40u
#define AP_I8237_CMD_DACK_ACTIVE_HIGH 0x80u

/* Mode register, `[8237]` Figure 5. Bits 1-0 select the channel and are not
 * stored in the channel's own mode byte. */
#define AP_I8237_MODE_TRANSFER 0x0Cu   /* bits 3-2 */
#define AP_I8237_MODE_AUTOINIT 0x10u
#define AP_I8237_MODE_DECREMENT 0x20u
#define AP_I8237_MODE_SELECT 0xC0u     /* bits 7-6 */

typedef enum {
  AP_I8237_TRANSFER_VERIFY = 0u, /* 00 */
  AP_I8237_TRANSFER_WRITE = 1u,  /* 01 */
  AP_I8237_TRANSFER_READ = 2u,   /* 10 */
  AP_I8237_TRANSFER_ILLEGAL = 3u,/* 11, "Illegal" */
} ap_i8237_transfer_t;

typedef enum {
  AP_I8237_MODE_DEMAND = 0u,
  AP_I8237_MODE_SINGLE = 1u,
  AP_I8237_MODE_BLOCK = 2u,
  AP_I8237_MODE_CASCADE = 3u,
} ap_i8237_mode_t;

typedef struct {
  /* `[8237]`: "Each channel has a 16-bit Current Address register ... It may
   * also be reinitialized by an Autoinitialize back to its original value",
   * which is what the base registers hold. */
  uint16_t base_address;
  uint16_t base_count;
  uint16_t current_address;
  uint16_t current_count;
  uint8_t mode;
} ap_i8237_channel_t;

typedef struct {
  ap_i8237_channel_t channel[AP_I8237_CHANNELS];

  uint8_t command;
  uint8_t status;  /* bits 3-0 terminal count, bits 7-4 request */
  uint8_t request; /* software requests, bits 3-0 */
  uint8_t mask;    /* bits 3-0; 1 inhibits */
  uint8_t temporary;

  /* `[8237]`: "This command must be executed prior to writing or reading new
   * address or word count information to the 8237A." One flip-flop for the
   * whole part, not one per channel -- so an interrupted two-byte sequence
   * corrupts the *next* channel programmed, not only its own. */
  bool high_byte;

  /* The DREQ pins, kept apart from the software request register because
   * `[8237]` treats them as separate sources that the priority encoder sees
   * alike. */
  uint8_t dreq;
} ap_i8237_t;

/* `[8237]`: reset "clears the Command, Status, Request, Temporary, and Internal
 * First/Last Flip-Flop registers and sets the Mask register." */
void ap_i8237_reset(ap_i8237_t *dma);

[[nodiscard]] uint8_t ap_i8237_read(ap_i8237_t *dma, unsigned reg);
void ap_i8237_write(ap_i8237_t *dma, unsigned reg, uint8_t value);

/* Drive a DREQ pin. */
void ap_i8237_set_request_pin(ap_i8237_t *dma, unsigned channel, bool asserted);

/* A channel's decoded mode. */
[[nodiscard]] ap_i8237_mode_t ap_i8237_mode_of(const ap_i8237_t *dma,
                                               unsigned channel);
[[nodiscard]] ap_i8237_transfer_t ap_i8237_transfer_of(const ap_i8237_t *dma,
                                                       unsigned channel);

/* The highest-priority channel asking for service, or -1. Honours the mask, the
 * controller-disable bit, and `[8237]`'s fixed or rotating priority. This is
 * the hand-off point to the arbiter when one exists; nothing here moves data. */
/* The electrical level on a channel's `DREQ` pin, and on its `DACK`.
 *
 * The command register's top two bits are the *polarity* of these lines --
 * bit 6 `DREQ Sense Active Low`, bit 7 `DACK Sense Active High` -- and this
 * core stored both and used neither. A caller that only ever passes "the device
 * is requesting" to `ap_i8237_set_request_pin` never sees the difference, which
 * is why it went unnoticed: the *logical* request is polarity-independent and
 * the *pin* is not.
 *
 * So the polarity is expressed where it is meaningful, as the level a board
 * would measure, rather than folded into the request itself. A board that wires
 * an inverting buffer, or a probe that reads the pin, needs these; the
 * arbitration inside the controller does not, and is unchanged. */
[[nodiscard]] bool ap_i8237_dreq_level(const ap_i8237_t *dma, unsigned channel);
[[nodiscard]] bool ap_i8237_dack_level(const ap_i8237_t *dma, unsigned channel);
[[nodiscard]] int ap_i8237_service_pending(const ap_i8237_t *dma);

/* ---------------------------------------------------------------------------
 * The transfer cycle
 *
 * `[8237]`: a service cycle moves one byte between memory and the peripheral
 * the acknowledge selects, adjusts the channel's current address and word
 * count, and ends the whole transfer when the count expires.
 *
 * ## The part gives sixteen bits of address, and that is all it has
 *
 * An 8237A drives A0-A15. On an AT the upper bits come from a page register;
 * on this machine they come from the address translation map, which is
 * `board/ap_atmap.h`'s business and not this module's. So the cycle reports the
 * 16-bit address it drove and the caller composes the physical one -- the same
 * division as everywhere else here, where the part is the part and the board is
 * the board.
 *
 * ## What the peripheral side is, and is not
 *
 * A DMA read or write does not address the device: it is selected by `DACK`
 * and the data moves on `IOR`/`IOW` with no address at all. The callbacks
 * therefore take a *channel*, not an address, which is what the hardware does
 * and also what stops a caller inventing a register for a device to be read
 * from.
 *
 * ## Memory-to-memory is declined
 *
 * `[8237]`'s command bit 0 pairs channel 0 with channel 1 through the temporary
 * register. It is not modelled: the transfer is a two-cycle sequence with its
 * own rules for the address-hold bit, and nothing on this board is known to use
 * it. Declined rather than half-done -- `ap_i8237_transfer` refuses a service
 * cycle while the bit is set and says so, so a caller cannot mistake silence
 * for a transfer. Cost to close: implement the read-then-write pair with the
 * temporary register; small, and blocked on nothing but a reason.
 * ------------------------------------------------------------------------- */

/* Memory is addressed; the peripheral is selected. */
typedef uint8_t (*ap_i8237_memory_read_fn)(void *context, uint16_t address);
typedef void (*ap_i8237_memory_write_fn)(void *context, uint16_t address,
                                         uint8_t value);
typedef uint8_t (*ap_i8237_device_read_fn)(void *context, unsigned channel);
typedef void (*ap_i8237_device_write_fn)(void *context, unsigned channel,
                                         uint8_t value);

typedef struct {
  ap_i8237_memory_read_fn memory_read;
  ap_i8237_memory_write_fn memory_write;
  ap_i8237_device_read_fn device_read;
  ap_i8237_device_write_fn device_write;
  void *context;
} ap_i8237_bus_t;

/* What one service cycle did. `ran` false means no channel was asking, or the
 * one that was cannot transfer -- a cascade channel, or an illegal or
 * memory-to-memory mode -- and the other fields are then meaningless. */
typedef struct {
  bool ran;
  unsigned channel;
  uint16_t address;    /* the sixteen bits the part drove */
  bool wrote_memory;   /* which direction, for a caller that wants to know */
  bool terminal_count; /* the count expired on this cycle */
} ap_i8237_cycle_t;

/* Run one transfer. Whether the part is *allowed* to run one -- whether it has
 * the bus -- is the arbiter's question and the caller's to have answered; this
 * moves the byte. */
ap_i8237_cycle_t ap_i8237_transfer(ap_i8237_t *dma, const ap_i8237_bus_t *bus);

/* Signal terminal count on a channel: the effect an EOP has on the registers.
 * Separated from any transfer so the register behaviour -- status bit, mask set
 * unless autoinitialise, reload from the base registers -- can be exercised
 * and checked before a byte ever moves. */
void ap_i8237_terminal_count(ap_i8237_t *dma, unsigned channel);

#endif /* APOLLO_DEVICE_AP_I8237_H */
