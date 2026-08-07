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
 * Serial framing **is** modelled, and this paragraph used to say it was not.
 * `ap_mc68681_resample` reshapes a character that arrives at a mismatched baud
 * rate -- which is what a receiver sampling at its own bit centres actually
 * gets, and what lets the boot PROM's autobaud work at all; `MR1`'s width is
 * applied; parity is checked on enable and type together; `MR2`'s stop-bit
 * field is read; and the four channel modes differ in behaviour rather than in
 * name. What is *not* modelled is the wire below all that: there is no bit
 * clock and no shift register, so a character crosses in one step.
 */

#ifndef APOLLO_DEVICE_AP_MC68681_H
#define APOLLO_DEVICE_AP_MC68681_H

#include <stdbool.h>
#include <stdint.h>

#include "time/ap_time.h"

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

/* `MR1[6]`, the **RxRDY/FFULL select**, and the reason the ISR's bit 1 is
 * labelled `RxRDY/FFULLA` in Table 4-5 rather than `RxRDY`.
 *
 * Clear, the interrupt bit follows `RxRDY` -- a character has arrived. Set, it
 * follows `FFULL` -- the receive FIFO is *full*, three characters deep on this
 * part. A host that selects `FFULL` and waits for a single byte waits for ever,
 * which is not a hypothetical: the boot PROM's `KEYBOARD TEST # 0` polls this
 * bit sixty-five thousand times and fails the machine when it never sets.
 *
 * This core did not model the select at all -- not declined, not commented,
 * simply absent, which is why the sweep for phrases like "not modelled" did not
 * find it. */
#define AP_MC68681_MR1_RXRDY_IS_FFULL 0x40u
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
  /* The FIFO's **status portion**, one entry per character. §4.2.1.3 makes the
   * three FIFOed status bits -- framing error, parity error and received break
   * -- a property of the character "at the top of the FIFO" in character mode,
   * so they have to travel with the character rather than sit in one register.
   * §3.4 names the same structure from the other side: in multidrop the
   * address/data bit is "loaded into the status portion of the FIFO stack
   * normally used for parity error". */
  uint8_t fifo_status[AP_MC68681_RX_FIFO];
  /* The flags the character now arriving has caused, before they are stored
   * with it. Separate from `sr` because `sr` is an accumulation and this is
   * one character's own. */
  uint8_t pending_status;
  unsigned fifo_count;
  bool rx_enabled;
  bool tx_enabled;
  /* The last character handed to the transmitter, so a caller can observe what
   * the port would have sent without a wire existing. */
  uint8_t tx_holding;
  bool tx_holding_full;
  /* §4.2.7.2's START BREAK, held until STOP BREAK. State without a consumer:
   * nothing in this machine watches TxD at bit level. Kept so the pair is
   * answered rather than ignored -- and named here so a reader can tell the
   * difference between a bit that is stored and a bit that does something. */
  bool tx_break;
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

/* ## The framing the mode registers describe
 *
 * `MR1` bits 1-0 give the character length, bits 4-3 the parity type and bit 2
 * whether parity is used at all; `MR2` bits 3-0 give the stop-bit length.
 * Decoded here as names and bit positions, the same way the display
 * controller's mode fields were, because that part is settleable before any of
 * it shapes a character on a wire -- and a field read from the wrong bits is a
 * defect that survives every test of the layer above it.
 *
 * ## Character length is a count, not a code
 *
 * `00` is five bits and `11` is eight, so the field is `5 + value`. Reading it
 * as an index into a table happens to work and hides that; reading it as a
 * count says why `11` is eight and not, say, nine.
 *
 * ## The stop-bit field is not two values
 *
 * `MR2[3:0]` is sixteen encodings covering 0.5 to 2 stop bits in sixteenths,
 * not a one-or-two flag. Only the two common lengths are named; the rest are
 * reachable and reported as their raw code rather than being folded into the
 * nearest named one, because a driver that programmed 1.5 stop bits meant it. */
#define AP_MC68681_MR1_BITS_MASK 0x03u

/* ## The two parity fields, which this file had **exchanged**
 *
 * Table 4-5 sheet 1 lays `MR1` out as: bit 7 RxRTS, bit 6 RxRDY/FFULL select,
 * bit 5 error mode, **bits 4-3 parity *mode***, **bit 2 parity *type***, bits
 * 1-0 bits-per-character.
 *
 *     mode 0 0  With Parity      type 0 = Even   1 = Odd
 *          0 1  Force Parity          0 = Low    1 = High
 *          1 0  No Parity
 *          1 1  Multidrop Mode        0 = Data   1 = Address
 *
 * This file had the *enable* at bit 2 and the *type* at bits 4-3 -- the two
 * fields swapped -- so "is parity on" read the even/odd bit and the answer was
 * whatever the type happened to be. Getting parity wrong on a UART is an old
 * bug and this is the ordinary way to arrive at it: two adjacent fields in one
 * register, and a table read from a summary rather than the layout.
 *
 * The mode is a four-value field and not a flag, which is the other half of the
 * error: `Force Parity` and `Multidrop` are neither "on" nor "off", and a
 * boolean cannot carry them. */
#define AP_MC68681_MR1_PARITY_MODE_MASK 0x18u
#define AP_MC68681_MR1_PARITY_MODE_SHIFT 3u
#define AP_MC68681_MR1_PARITY_MODE_WITH 0u
#define AP_MC68681_MR1_PARITY_MODE_FORCE 1u
#define AP_MC68681_MR1_PARITY_MODE_NONE 2u
#define AP_MC68681_MR1_PARITY_MODE_MULTIDROP 3u
#define AP_MC68681_MR1_PARITY_TYPE 0x04u /* 0 = even, 1 = odd */

/* `MR1[7]`, receiver request-to-send control. §4.2.1.1: it exists to "prevent
 * overrun in the receiver by using the RTSA output signal to control the
 * clear-to-send CTS input of the transmitting device" -- so with it set, the
 * receiver negates RTS when its FIFO fills, and the far end stops sending. */
#define AP_MC68681_MR1_RX_RTS 0x80u

/* `MR1[5]`, error mode. §4.2.1.3: it "selects the operating mode of the three
 * FIFOed status bits" -- framing error, parity error and received break.
 *
 *   character mode -- status "is given on a character-by-character basis and
 *                     applies only to the character at the top of the FIFO"
 *   block mode     -- status is "the accumulation (logical OR) of the status
 *                     for all characters coming to the top of the FIFO since
 *                     the last reset error status command"
 *
 * The difference is observable: in block mode one bad character leaves the
 * error set until `RESET ERROR STATUS`, and in character mode the next good
 * character clears it. */
#define AP_MC68681_MR1_ERROR_BLOCK 0x20u

/* `MR2[5]`, transmitter request-to-send control. §4.2.2.2: with it set,
 * `OPR[0]` "is cleared automatically one bit time after the characters in the
 * ... transmit shift register and in the transmit holding register, if any, are
 * completely transmitted", which is how a driver ends a message without
 * watching for the last character itself. */
#define AP_MC68681_MR2_TX_RTS 0x20u

/* `MR2[4]`, clear-to-send control. §4.2.2.3: "If this bit is zero, channel A
 * clear-to-send control (CTSA) has no effect on the transmitter. If this bit is
 * a one, the transmitter checks the state of CTSA (IP0) each time it is ready
 * to send a character. If IP0 is asserted (low), the character is transmitted.
 * If it is negated (high), the ... serial-data output remains in the marking
 * state and the transmission is delayed until CTSA goes low."
 *
 * **Asserted is low**, so an input port reading zero on that pin means clear to
 * send. A model that read it the other way round would hold off exactly when
 * the hardware transmits. */
#define AP_MC68681_MR2_CTS_ENABLE 0x10u

/* CTS is `IP0` for channel A and `IP1` for channel B; RTS is `OP0` and `OP1`.
 * §4.2.2.2 and §4.2.2.3 name channel A's; channel B's follow the part's
 * convention that the second channel takes the next pin. */
#define AP_MC68681_IP_CTS(channel) ((uint8_t)(1u << (channel)))
#define AP_MC68681_OP_RTS(channel) ((uint8_t)(1u << (channel)))
#define AP_MC68681_MR2_STOP_MASK 0x0Fu

/* `MR2[3:0]`: the two lengths a console link uses. */
#define AP_MC68681_MR2_STOP_ONE 0x07u
#define AP_MC68681_MR2_STOP_TWO 0x0Fu

/* Bits per character, 5 to 8, from `MR1[1:0]`. */
/* The bit rate a clock-select nibble names, `[68681]`'s baud rate generator
 * table, or zero for the four codes that are not a fixed rate -- the timer and
 * the two external-clock selections.
 *
 * `ACR[7]` picks between the two published sets. They agree on every code this
 * machine's firmware uses, which is why the autobaud below can be reasoned
 * about without settling which set is in force. */
[[nodiscard]] unsigned ap_mc68681_baud(uint8_t csr_nibble, bool acr_set_two);

/* The byte a receiver running at `receiver_baud` actually sees when a sender
 * transmits `byte` at `sender_baud`.
 *
 * ## Why this exists, and what it fixes
 *
 * A rate mismatch was modelled as a *flag*: the byte arrived intact and `SR[6]`
 * was set beside it. That is not what a UART does. The receiver finds the start
 * edge and then samples at the bit centres its **own** clock predicts, so at the
 * wrong rate it samples the sender's waveform at the wrong instants and returns
 * a different value. The flag is a consequence of where the stop bit landed, not
 * the whole of the effect.
 *
 * The difference is the entire boot PROM console negotiation. Its autobaud
 * compares the received byte against `FF`, `FE`, `C7`, `72` and `C0` -- the
 * shapes a carriage return takes at five wrong rates -- and writes a different
 * clock select for each. A model delivering `0D` intact matches none of them,
 * so the firmware loops forever having learned nothing, which is exactly what
 * this core did. `FINDINGS.md` C109.
 *
 * ## The model
 *
 * Start bit low, `bits` data bits least significant first, stop bit high, each
 * one sender-bit-time wide. The receiver samples bit `i` at `(i + 1.5)`
 * receiver-bit-times after the start edge -- the middle of where it believes
 * that bit to be. Sampling past the end of the sender's stop bit reads the idle
 * line, which is high.
 *
 * Equal rates give the byte back unchanged, which is the property that keeps
 * every existing correctly-configured link exactly as it was. */
[[nodiscard]] uint8_t ap_mc68681_resample(uint8_t byte, unsigned bits,
                                          unsigned sender_baud,
                                          unsigned receiver_baud);

[[nodiscard]] unsigned ap_mc68681_character_bits(uint8_t mr1);

/* Whether `MR1` asks for a parity bit at all. Bit 2 **clear** means with
 * parity, which is the inversion most easily got backwards -- and getting it
 * backwards yields a link that works until the first character with an odd
 * number of set bits. */
[[nodiscard]] bool ap_mc68681_parity_enabled(uint8_t mr1);

/* Stop-bit code from `MR2[3:0]`, raw. Compare against
 * `AP_MC68681_MR2_STOP_ONE` and `_TWO` rather than converting to a count: the
 * field's other values are fractional and a count cannot carry them. */
[[nodiscard]] unsigned ap_mc68681_stop_code(uint8_t mr2);

/* The stop-bit length in **sixteenths of a bit time**, from `[68681]` Table 4-5
 * (the MR2 sheet), read from the page image because the extraction turns
 * `0.563` into `0:563`.
 *
 * Sixteenths rather than a fraction because every entry is an exact one: 0.563
 * is 9/16, 1.063 is 17/16, 2.000 is 32/16. Carrying them as a rounded decimal
 * would lose the exactness the table has, and a character time built from
 * rounded parts is a character time that drifts.
 *
 * The table has **two columns**, and which applies depends on `MR1`: a 5-bit
 * character adds half a bit to codes 0-7 and leaves 8-15 alone. So this takes
 * both registers -- a stop length read from `MR2` alone is right for three of
 * the four character lengths and quietly wrong for the fourth. */
[[nodiscard]] unsigned ap_mc68681_stop_sixteenths(uint8_t mr1, uint8_t mr2);

/* How long one character occupies the line, in `AP_TIME_BASE_HZ` units: the
 * start bit, the data bits, the parity bit if `MR1` asks for one, and the stop
 * length above.
 *
 * This is the figure the tick loop's first named debt wanted. A caller pacing
 * input at ten bit times per character is right only for 8N1 -- it is wrong by
 * a whole bit with parity enabled, and by up to a further bit at the stop
 * length's extremes. Zero when the clock select names no fixed rate, which is
 * a refusal rather than a division by zero. */
[[nodiscard]] ap_time_t ap_mc68681_character_time(uint8_t mr1, uint8_t mr2,
                                                  unsigned baud);

/* `MR2[7:6]`, the channel mode. Normal is a wire to the outside; the other
 * three connect the channel to itself in different places, and a self-test uses
 * them to check the part without anything attached. */
typedef enum {
  AP_MC68681_MODE_NORMAL = 0u,
  AP_MC68681_MODE_AUTO_ECHO = 1u,
  AP_MC68681_MODE_LOCAL_LOOPBACK = 2u,
  AP_MC68681_MODE_REMOTE_LOOPBACK = 3u,
} ap_mc68681_channel_mode_t;

[[nodiscard]] ap_mc68681_channel_mode_t ap_mc68681_channel_mode(uint8_t mr2);

/* Receive a character whose sender states its *whole* framing, not only its
 * rate: `sender_mr1` carries the parity enable and type the far end is using.
 * A disagreement sets `SR[5]`, parity error.
 *
 * Separate from `ap_mc68681_receive_at` rather than replacing it, because the
 * two say different things. `receive_at` means "the sender agrees about
 * framing and we are checking the rate", which is what a scripted terminal on
 * a configured link is; this means "here is the far end's configuration, decide
 * whether it can be read". A caller that had to pass the receiver's own `MR1`
 * to say "the same" would be stating a fact it does not have.
 *
 * Parity is compared as *enable and type together*. Two ports both using
 * parity but disagreeing on odd against even produce a wrong parity bit on
 * roughly half of all characters, which is a link that works intermittently --
 * far worse than one that never works, and invisible to a test that sends a
 * single character. */
void ap_mc68681_receive_framed(ap_mc68681_t *duart, unsigned channel,
                               uint8_t byte, uint8_t sender_csr,
                               uint8_t sender_mr1);

/* Take what the transmitter holds, if anything -- the other end of the same
 * boundary. Answers false when the transmitter is empty. */
[[nodiscard]] bool ap_mc68681_transmit(ap_mc68681_t *duart, unsigned channel,
                                       uint8_t *byte);

/* Drive the input port pins. A change sets the input port change register and
 * `ISR[7]`. */
void ap_mc68681_set_input(ap_mc68681_t *duart, uint8_t value);

/* The level on an output pin, `OP0` to `OP7`.
 *
 * Table 4-5 sheet 3 gives `OPCR` six independent selects, and this core acted
 * on one of them -- `OPCR[7]`, because a board register happened to need it.
 * The other five were stored and inert, which is the same defect in five
 * places:
 *
 *     OP7  0 = OPR[7]   1 = TxRDYB
 *     OP6  0 = OPR[6]   1 = TxRDYA
 *     OP5  0 = OPR[5]   1 = RxRDYB/FFULLB
 *     OP4  0 = OPR[4]   1 = RxRDYA/FFULLA
 *     OP3  00 = OPR[3]  01 = C/T output   10 = TxCB(1X)  11 = RxCB(1X)
 *     OP2  00 = OPR[2]  01 = TxCA(16X)    10 = TxCA(1X)  11 = RxCA(1X)
 *
 * `OP1` and `OP0` have no select and are always their `OPR` bit -- §4.2.11's
 * note adds that they double as the two channels' RTS lines when `MR1[7]` or
 * `MR2[5]` asks for it, which is what `AP_MC68681_OP_RTS` drives.
 *
 * **The pin is the complement of the register bit**, which sheet 5's output
 * port table shows by overbarring every `OPR` entry. So this returns the *pin*
 * level and a caller reading `opr` directly gets the opposite.
 *
 * The clock sources on `OP3`'s and `OP2`'s upper codes are not modelled: this
 * core has no bit clock to put on a pin, and a level invented for one would be
 * a claim about a waveform that does not exist. Those codes return false and
 * say so here rather than silently reading as an `OPR` bit. */
[[nodiscard]] bool ap_mc68681_output_pin(const ap_mc68681_t *duart,
                                         unsigned pin);

/* One counter/timer clock tick. */
void ap_mc68681_clock(ap_mc68681_t *duart);

/* The IRQ pin: any interrupt status bit whose mask bit is set. */
[[nodiscard]] bool ap_mc68681_irq(const ap_mc68681_t *duart);

/* Whether the counter/timer is in a timer mode, `[68681]` ACR[6:4]. In timer
 * mode "the timer runs continuously and cannot be started or stopped by the
 * CPU", which changes what the two command addresses do. */
[[nodiscard]] bool ap_mc68681_timer_mode(const ap_mc68681_t *duart);

#endif /* APOLLO_DEVICE_AP_MC68681_H */
