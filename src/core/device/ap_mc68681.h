/* MC68681 / SCN2681 dual asynchronous receiver-transmitter.
 *
 * `[68681]` *MC68681 Dual Asynchronous Receiver/Transmitter (DUART)*, Motorola,
 * Sep 1985. `008778-03` §3.9: "All ports are implemented using the Signetics
 * 2681 dual asynchronous control chip", and `[68681]` §1 says the two are the
 * same part -- "The MC2681 ... is functionally equivalent to the MC68681 with
 * some minor differences."
 *
 * ## What is modelled
 *
 * The programming model: all sixteen register addresses of `[68681]` Table 4-1,
 * both channels' mode registers with their shared pointer, the clock-select,
 * command and status registers, the receive FIFO, the interrupt status and mask
 * registers, the input and output ports, and the counter/timer including its
 * two address-triggered commands.
 *
 * ## What is not
 *
 * Serial framing itself -- baud rates, start and stop bits, parity, and the four
 * channel modes (normal, automatic echo, local and remote loopback). Those
 * describe what happens on a wire, and no wire is connected: a character is
 * handed to this module whole and taken from it whole. `ap_mc68681_receive`
 * exists for whatever eventually drives the line.
 *
 * That boundary is worth stating precisely because it is *not* where the value
 * is. `008778-03` §3.9 has the counter/timer driving memory refresh -- "set up
 * in the timer mode to produce a square wave output on output OP3. The period
 * of the output is 15 microseconds" -- so on this board the DUART is a system
 * component before it is a serial port, and the counter is modelled properly
 * even though the framing is not.
 *
 * ## Reads with side effects
 *
 * More of this part's registers change state on being read than not, and a
 * caller must know it: the input port change register clears, the receive
 * buffer pops the FIFO, and addresses 14 and 15 are *commands* taken on a read.
 * `FINDINGS.md` C14 records catching this in the oracle -- a register dump of
 * the real machine started a counter as a side effect of being taken.
 *
 * `[68681]` also marks read addresses 2 and 10 "Do Not Access": "This address
 * location is used for factory testing of the DUART and should not be read.
 * Reading this location will result in undesired effects and possible incorrect
 * transmission or reception of characters." This core returns zero and changes
 * nothing, which is the one behaviour that cannot be wrong in a way that
 * matters -- the hardware's own answer is explicitly undefined.
 */

#ifndef APOLLO_DEVICE_AP_MC68681_H
#define APOLLO_DEVICE_AP_MC68681_H

#include <stdbool.h>
#include <stdint.h>

#define AP_MC68681_REGISTERS 16u
#define AP_MC68681_CHANNELS 2u

/* `[68681]` §1: "Quadruple-Buffered Receiver Data Registers" -- three FIFO
 * positions behind the receive shift register. */
#define AP_MC68681_RX_FIFO 3u

/* Table 4-1's addresses. */
typedef enum {
  AP_MC68681_MR_A = 0u,       /* R/W mode register A */
  AP_MC68681_SR_CSR_A = 1u,   /* read status A, write clock select A */
  AP_MC68681_CR_A = 2u,       /* write command A; read is "Do Not Access" */
  AP_MC68681_RB_TB_A = 3u,    /* read receive buffer A, write transmit A */
  AP_MC68681_IPCR_ACR = 4u,   /* read input port change, write auxiliary */
  AP_MC68681_ISR_IMR = 5u,    /* read interrupt status, write interrupt mask */
  AP_MC68681_CUR_CTUR = 6u,   /* read counter MSB, write preload upper */
  AP_MC68681_CLR_CTLR = 7u,   /* read counter LSB, write preload lower */
  AP_MC68681_MR_B = 8u,
  AP_MC68681_SR_CSR_B = 9u,
  AP_MC68681_CR_B = 10u,
  AP_MC68681_RB_TB_B = 11u,
  AP_MC68681_IVR = 12u,       /* R/W interrupt vector */
  AP_MC68681_IP_OPCR = 13u,   /* read input port, write output port config */
  AP_MC68681_START_OPR_SET = 14u,  /* read starts counter, write sets OPR bits */
  AP_MC68681_STOP_OPR_CLEAR = 15u, /* read stops counter, write clears them */
} ap_mc68681_reg_t;

/* Status register, `[68681]` §4.2.9. */
#define AP_MC68681_SR_RXRDY 0x01u  /* receiver ready */
#define AP_MC68681_SR_FFULL 0x02u  /* §4.2.9.7, "FIFO Full - SRA[1]" */
#define AP_MC68681_SR_TXRDY 0x04u  /* §4.2.9.6, "Transmitter Ready - SRA[2]" */
#define AP_MC68681_SR_TXEMT 0x08u  /* §4.2.9.5, "Transmitter Empty - SRA[3]" */
#define AP_MC68681_SR_OVERRUN 0x10u
#define AP_MC68681_SR_PARITY 0x20u
#define AP_MC68681_SR_FRAMING 0x40u
#define AP_MC68681_SR_BREAK 0x80u

/* Interrupt status register, `[68681]` §4.2.15. */
#define AP_MC68681_ISR_TXRDY_A 0x01u
#define AP_MC68681_ISR_RXRDY_A 0x02u
#define AP_MC68681_ISR_BREAK_A 0x04u
#define AP_MC68681_ISR_COUNTER 0x08u /* §4.2.15.5, "Counter/Timer Ready" */
#define AP_MC68681_ISR_TXRDY_B 0x10u
#define AP_MC68681_ISR_RXRDY_B 0x20u
#define AP_MC68681_ISR_BREAK_B 0x40u
#define AP_MC68681_ISR_INPUT 0x80u   /* §4.2.15.1, "Input Port Change Status" */

/* Auxiliary control register, `[68681]` §4.2.13.2: "Counter/Timer Mode and
 * Clock Source Select - ACR[6:4]". Values 4 and above are the timer modes. */
#define AP_MC68681_ACR_CT_MODE 0x70u

typedef struct {
  uint8_t mr[2];      /* MR1 and MR2 */
  bool mr_pointer;    /* false selects MR1, true MR2 */
  uint8_t csr;        /* clock select */
  uint8_t sr;         /* status */
  uint8_t fifo[AP_MC68681_RX_FIFO];
  unsigned fifo_count;
  bool rx_enabled;
  bool tx_enabled;
  /* The last character handed to the transmitter, so a caller can observe what
   * the port would have sent without a wire existing. */
  uint8_t tx_holding;
  bool tx_holding_full;
} ap_mc68681_channel_t;

typedef struct {
  ap_mc68681_channel_t channel[AP_MC68681_CHANNELS];

  uint8_t acr;
  uint8_t imr;
  uint8_t isr;
  uint8_t ivr;
  uint8_t ipcr;   /* input port change; cleared by reading it */
  uint8_t opcr;
  uint8_t opr;    /* output port */
  uint8_t input;  /* the input port pins */

  uint16_t preload; /* CTUR:CTLR */
  uint16_t counter;
  bool counter_running;
  bool counter_output; /* the square wave, available on OP3 */
  /* Which half of the square wave's period the next terminal count ends.
   * `[68681]` sets the ready bit on every *second* terminal count, and that
   * phase must be tracked explicitly: inferring it from `counter_output` fails
   * because the start command inverts the output too, so the phase would depend
   * on where it happened to begin. */
  bool counter_second_half;
} ap_mc68681_t;

void ap_mc68681_reset(ap_mc68681_t *duart);

[[nodiscard]] uint8_t ap_mc68681_read(ap_mc68681_t *duart, unsigned reg);
void ap_mc68681_write(ap_mc68681_t *duart, unsigned reg, uint8_t value);

/* Hand a received character to a channel, as a wire would. */
void ap_mc68681_receive(ap_mc68681_t *duart, unsigned channel, uint8_t byte);

/* Receive a character sent at a stated rate, which is the first piece of real
 * framing this module has.
 *
 * `sender_csr` is the clock-select register value the *sender* is using; its
 * upper nibble is the receiver clock select, matching this channel's own `csr`.
 * When the two disagree the character was sampled at the wrong rate, so the
 * receiver does not see a valid stop bit: the byte still enters the FIFO -- the
 * part does not discard it -- and `SR[6]`, framing error, is set alongside it.
 *
 * That failure is not incidental. The DN3500's boot PROM finds its console by
 * **autobauding**: it cycles channel B's clock select and waits for a character
 * that decodes cleanly, so a model where every byte arrives intact whatever the
 * rate would let the negotiation succeed at the first rate tried and would
 * never reproduce what the machine does. The framing error is the signal the
 * firmware is actually reading. */
void ap_mc68681_receive_at(ap_mc68681_t *duart, unsigned channel, uint8_t byte,
                           uint8_t sender_csr);

/* Take what the transmitter holds, if anything -- the other end of the same
 * boundary. Answers false when the transmitter is empty. */
[[nodiscard]] bool ap_mc68681_transmit(ap_mc68681_t *duart, unsigned channel,
                                       uint8_t *byte);

/* Drive the input port pins. A change sets the input port change register and
 * `ISR[7]`. */
void ap_mc68681_set_input(ap_mc68681_t *duart, uint8_t value);

/* One counter/timer clock tick. */
void ap_mc68681_clock(ap_mc68681_t *duart);

/* The IRQ pin: any interrupt status bit whose mask bit is set. */
[[nodiscard]] bool ap_mc68681_irq(const ap_mc68681_t *duart);

/* Whether the counter/timer is in a timer mode, `[68681]` ACR[6:4]. In timer
 * mode "the timer runs continuously and cannot be started or stopped by the
 * CPU", which changes what the two command addresses do. */
[[nodiscard]] bool ap_mc68681_timer_mode(const ap_mc68681_t *duart);

#endif /* APOLLO_DEVICE_AP_MC68681_H */
