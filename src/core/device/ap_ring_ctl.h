/* Apollo Token Ring controller: the register interface.
 *
 * Sources are `docs/references/RING.md`, whose numbered findings are cited
 * throughout rather than restated. The register map came out of the ring
 * option ROM's own disassembly -- `[ROM3500]`, Apollo part 10666 -- because no
 * register-level document for this board exists on disk or on the web.
 *
 * ## A unit is two windows, not one
 *
 * Finding 38: the firmware's `$CA0` maps a unit number to **two** base
 * pointers. Unit 0 gets `a1 = $51000` (AT I/O `0x220`) *and* `a2 = $59000` (AT
 * `0x320`); unit 1 gets `$52000`/`$5A000`. Anything else returns zero and the
 * caller reports `ring: init error`. So `[S3K]` Table 2-9's "a second
 * controller sits at AT 0x320" is how the manual lists the ranges, and the
 * firmware drives the pair as one unit.
 *
 * Whether that is one board with two decodes or two boards driven as a pair is
 * **not settled** by the ROM, and this module does not decide it: it models a
 * unit with two windows and leaves the question where the evidence leaves it.
 *
 * ## The four banks
 *
 * Finding 12: four banks at `+000`, `+400`, `+800` and `+C00`, each with slots
 * at `+0`, `+2`, `+4`, `+6`. Fifteen distinct offsets are touched on the DN3500
 * and DN4500 ROMs and only the first bank's four on the DN3000's, so the bank
 * structure is the later board's.
 *
 * ## What each bank is, and what is still unknown
 *
 *   `+000`  an **ID register**. Finding 39: init reads a *byte* from `(a2)`,
 *           after clearing the host instruction cache and with three
 *           `and.w #$ffff,d0` as a delay, and accepts only `$36` or `$37` --
 *           ASCII `'6'` and `'7'`. Anything else is `ring: init error`.
 *   `+400`  a **status word whose bit 15 is a presence gate**. Findings 13 and
 *           40: init reads it, masks `$8000`, and if clear returns success
 *           having touched nothing else -- an empty slot is not an error. With
 *           it set the firmware clears `(a2)`, `+402`, `+404`, `+400` in that
 *           order, and later writes `$800` (bit 11) to `+400`.
 *   `+800`  an **Intel 8254**, counters at `+0`/`+2`/`+4` and control at `+6`.
 *   `+C00`  a second 8254, driven with the same pattern immediately after.
 *
 * Finding 41 establishes the timers on four independent points, and 41a records
 * that this corrects an earlier reading of the same offsets as buffer.
 *
 * ## The buffer is a port, not a window
 *
 * Findings 46, 46a and 47, from the self-test at `ENTRY_05`. The dual-ported RAM
 * `[S3K]` §1.5.4 promises is **not memory-mapped**: it is reached through an
 * auto-incrementing data port at `+406`, whose pointer is `+006`. The firmware
 * walks `$7FFF + 1` words -- **64 KB** -- in four patterns, resetting `+006` to
 * zero before each pass.
 *
 * This corrects finding 42, in the way 41a corrected 15. That scan was right
 * that no absolute long operand in the ROM lies in AT *memory* space; its
 * conclusion that the buffer was therefore unreachable assumed a dual-ported
 * buffer had to be memory-mapped, and it is not.
 *
 * **The read port is pipelined by one word, and that is not a detail.** Both
 * readers discard a `move.w (a1),d1` before their loop and then read `d3 + 1`
 * words. A model that returned the addressed word immediately would be one word
 * out for the whole 64 KB and fail every pattern the firmware tries.
 *
 * `+006` is modelled as a plain word pointer, which is what the memory test
 * makes observable and what the transmit and receive paths corroborate:
 * transmit sets it to `0` and writes a four-word header there, receive sets it
 * to `$10` and reads the frame back from word 16 -- a coherent layout with the
 * transmit area at the base. The other observed values are `$600` and `$610`,
 * both inside the tested extent. Whether its upper bits also carry a *mode* is
 * **not settled**: no access distinguishes that reading from a pointer, so the
 * simpler one is modelled and the ambiguity recorded rather than decided.
 *
 * ## What is still unknown, and modelled as storage rather than guessed at
 *
 * `+400`'s bits. Finding 45 shows five of them live -- 15, 13, 11, 2 and 1 --
 * each polled by its own helper with its own timeout and an expected polarity,
 * and *none* of them explained. `+402` and `+404` are byte-wide command
 * registers taking `$1`, `$2`, `$6` and `$8`, `$0`, and carry status as well;
 * their bits are equally unexplained. All of it is kept so a driver reads back
 * what it wrote, and nothing here acts on any of it. Question A in `RING.md`.
 *
 * The `a1` window is **write-only to this firmware** -- finding 50a is an
 * exhaustive scan finding no read of any `(a3)` offset anywhere in the ROM -- so
 * its registers cannot be characterised further from this source, and it is
 * storage here for that reason rather than for want of looking.
 *
 * ## What this is not
 *
 * The transmit and receive logic and the bypass relays. Finding 50 gives their
 * shape -- the self-test is a **loopback** test, transmitting a frame and
 * expecting to receive its own, which is what §3.5's relay arrangement exists to
 * permit -- but the status bits that sequence it are the unknown above. The MAC
 * layer already exists in `src/core/ring/`; what is missing is the meaning of
 * the handshake between it and these registers.
 */

#ifndef APOLLO_DEVICE_AP_RING_CTL_H
#define APOLLO_DEVICE_AP_RING_CTL_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_i8254.h"
#include "ring/ap_ring_medium.h"
#include "ring/ap_ring_phy.h"
#include "ring/ap_ring_station.h"
#include "time/ap_time.h"

/* Finding 38's four windows, as Apollo physical addresses. */
#define AP_RING_CTL_UNIT0_A1 0x051000u
#define AP_RING_CTL_UNIT0_A2 0x059000u
#define AP_RING_CTL_UNIT1_A1 0x052000u
#define AP_RING_CTL_UNIT1_A2 0x05A000u
#define AP_RING_CTL_WINDOW 0x1000u

/* Finding 12's banks. */
#define AP_RING_CTL_BANK_ID 0x000u
#define AP_RING_CTL_BANK_STATUS 0x400u
#define AP_RING_CTL_BANK_TIMER_A 0x800u
#define AP_RING_CTL_BANK_TIMER_B 0xC00u
#define AP_RING_CTL_BANK_MASK 0xC00u
#define AP_RING_CTL_SLOT_MASK 0x006u

/* ## `+400` is **MISC_STAT**, and every bit is named -- `RING.md` 93
 *
 * These were "named by number because that is all that is established" until
 * `[EH]` ch. 12 was read: `002398-04` p. 12-29 tabulates this board's ring
 * registers by physical address, and p. 12-30 gives `59400`'s bits outright.
 * The firmware-derived model needed no change; the names are what it lacked.
 *
 * **Two of them are active low, which the numbers hid**: `xby` and `rby` are
 * "0=> xmt busy" and "0=> rcv busy", so a *clear* bit means busy. That is why
 * finding 66's `$6` command clearing bit 13 is a transmit *starting*, and why
 * finding 74's acknowledge putting bits 2 and 1 back is an interrupt being
 * *cleared* -- both read backwards from the bit alone. */
#define AP_RING_CTL_STATUS_PRESENT 0x8000u /* nct, 0 => network connect */
#define AP_RING_CTL_STATUS_TMO 0x4000u     /* timeout */
#define AP_RING_CTL_STATUS_XBY 0x2000u     /* 0 => transmit busy */
#define AP_RING_CTL_STATUS_RBY 0x1000u     /* 0 => receive busy */
#define AP_RING_CTL_STATUS_IOV 0x0800u     /* 1 => initialize overrun */
#define AP_RING_CTL_STATUS_RLK 0x0400u     /* receive lock error */
/* Which 8254 counter is which, from `[EH]` p. 12-32's names and offsets
 * (finding 41a). Timer A is the receive trio at `+800`/`+802`/`+804` and timer
 * B the transmit trio at `+C00`/`+C02`/`+C04`; each counter has its own CLK
 * pin, driven by its own event -- p. 12-30's `RCV_STAT` bits 2:0 name the three
 * receive conditions one for one. */
/* **And what each one counts, which no manual states**: `ring8a.drvr` carries a
 * six-entry descriptor table at `61CC`, stride 16, in exactly p. 12-29's order,
 * pairing each counter's short name with a description that names its **unit**
 * (`RING.md` 100):
 *
 *     RCV_HDR   "Receiver Header Byte"        bytes
 *     RCV_DAT   "Receiver Data Byte"          bytes   -- p. 12-29's RCV_PKT_CNT
 *     RCV_MAX   "Receiver Maximum Word"       words
 *     XMT_HDR   "Transmitter Header Word"     words
 *     XMT_PKT   "Transmitter Total Word"      words
 *     ERR_PKT   "Received Error Packet"       packets
 *
 * Three different units across six counters, which is what findings 80c and
 * 81b were missing: they asked what advances `RCV_HDR_CNT` three fewer times
 * than `RCV_PKT_CNT` while assuming a common unit. The receive pair really do
 * share one (bytes); `RCV_MAX_CNT` does not. Note also that p. 12-29's
 * `RCV_PKT_CNT` counts **data bytes**, not packets -- the name misleads and the
 * driver's does not. */
#define AP_RING_CTL_RCV_HDR_CNT 0u
#define AP_RING_CTL_RCV_PKT_CNT 1u
#define AP_RING_CTL_RCV_MAX_CNT 2u
#define AP_RING_CTL_XMIT_HDR_CNT 0u
#define AP_RING_CTL_XMIT_PKT_CNT 1u
#define AP_RING_CTL_BAD_PKT_CNT 2u

#define AP_RING_CTL_STATUS_ESB 0x0200u     /* sticky elastic-store error */
#define AP_RING_CTL_STATUS_BPE 0x0100u     /* sticky bi-phase error */
#define AP_RING_CTL_STATUS_GPS 0x0008u     /* sticky good packet seen */
#define AP_RING_CTL_STATUS_XI 0x0004u      /* XMIT interrupt pending */
#define AP_RING_CTL_STATUS_RI 0x0002u      /* RCV interrupt pending */
#define AP_RING_CTL_STATUS_TMI 0x0001u     /* gate-array timeout interrupt */

/* What `+400` reads on an idle, just-reset board.
 *
 * The firmware's subtest 01 asserts `(+400) & $F806 == $F806` -- bits 15, 14,
 * 13, 12, 11, 2 and 1 -- and says nothing about the rest, because its mask
 * does not reach them.
 *
 * **Bit 0 `tmi` is set here on the kernel driver's evidence, not the ROM's**
 * (`RING.md` 111). `RING_PROC` at `7A4D0944` reads MISC_STAT, shifts bit 0 into
 * carry and branches *past* its error call when the bit is **set** -- so a
 * healthy board reads 1 and a clear bit is the pending gate-array timeout its
 * `<=0` notation describes. The self-test is byte-identical either way, which
 * is what makes this a reading of a bit the firmware never constrains rather
 * than a change to one it does. `gps` at bit 3 stays clear: p. 12-30 marks it
 * `<=1`, active high, so an idle board has seen no good packet yet. */
#define AP_RING_CTL_STATUS_IDLE 0xF807u

/* ## `+402` is **XMIT_STAT**, one register with two layouts -- `RING.md` 93c,
 * 97
 *
 * `002398-04` p. 12-31, read as a page image. The low byte is fixed and bit 15
 * `pe` selects what bits 14-8 mean, which is why a single flat table of this
 * register cannot be written. Bits 3 and 2 are marked `x` and are not named.
 *
 * The polarity notation is the manual's own and four of the six named low bits
 * are **active low**: "network connect <= 0", "xmt enable <=1", "initialize
 * busy <=0", "xmt busy <=0", and both tag bits "<=0". */
#define AP_RING_CTL_XMIT_PE 0x8000u  /* protocol error, selects bits 14-8 */
#define AP_RING_CTL_XMIT_ABT 0x0200u /* packet aborted -- both layouts */
#define AP_RING_CTL_XMIT_IFE 0x0100u /* interface error, iff abt -- both */
#define AP_RING_CTL_XMIT_NCT 0x0080u /* 0 => network connect */
#define AP_RING_CTL_XMIT_XEN 0x0040u /* 1 => transmit enable */
#define AP_RING_CTL_XMIT_IBY 0x0020u /* 0 => initialize busy */
#define AP_RING_CTL_XMIT_XBY 0x0010u /* 0 => transmit busy */
#define AP_RING_CTL_XMIT_XT1 0x0002u /* 0 => transmit tag bit 1 */
#define AP_RING_CTL_XMIT_XT0 0x0001u /* 0 => transmit tag bit 0 */

/* Bits 14-8 when `pe` is 0. Note `pke` and `de` sit at 11 and 10 here and at
 * **10 and 11** in RCV_STAT below -- the two registers transpose them, which is
 * exactly the mistake a header written from one table and applied to both
 * would make. */
#define AP_RING_CTL_XMIT_CPD 0x4000u /* copied */
#define AP_RING_CTL_XMIT_WAK 0x2000u /* wait acknowledge */
#define AP_RING_CTL_XMIT_ICP 0x1000u /* icopy */
#define AP_RING_CTL_XMIT_PKE 0x0800u /* packet error (pkterr/ackbyte_errbit) */
#define AP_RING_CTL_XMIT_DE 0x0400u  /* data error (crc_error) */

/* And bits 14-8 when `pe` is 1. */
#define AP_RING_CTL_XMIT_TMO 0x4000u /* timeout in the gate array */
#define AP_RING_CTL_XMIT_SYN 0x2000u /* sync error */
#define AP_RING_CTL_XMIT_ERN 0x1000u /* error return (no_return) */
#define AP_RING_CTL_XMIT_FRM 0x0800u /* from error */
#define AP_RING_CTL_XMIT_AKP 0x0400u /* ackbyte parity error */

/* The low lane of `+402`, which finding 48 says carries status beside the
 * command byte. Subtest 13 requires `(+402) & $F0 == $F0` after the firmware
 * has written only the high lane -- and p. 12-31 says what that word *is*,
 * exactly as 93h said what `+404`'s `E0` is: `nct` `xen` `iby` `xby` all set
 * is a **disconnected, transmit-enabled, not-initialize-busy, not-transmit-busy
 * board**, which is what a just-reset one is. The constant was carried as a
 * literal with "the rest are unasserted and stay clear" for as long as its
 * bits had no names. `ring_ctl_suite` asserts the decomposition so the two
 * cannot drift apart. */
#define AP_RING_CTL_COMMAND_STATUS_IDLE 0x00F0u

/* ## `+404` is **RCV_STAT**, and all sixteen bits are documented
 *
 * `002398-04` p. 12-30, read as a page image. Finding 93h recorded the low byte
 * only -- the page gives the high byte on the same diagram, and unlike
 * XMIT_STAT there is **one layout**: `pe` at bit 15 is a status bit here, not a
 * selector. */
#define AP_RING_CTL_RCV_PE 0x8000u  /* protocol error (timeout_rs) */
#define AP_RING_CTL_RCV_CPD 0x4000u /* copied */
#define AP_RING_CTL_RCV_WAK 0x2000u /* wait acknowledge */
#define AP_RING_CTL_RCV_ICP 0x1000u /* icopy */
#define AP_RING_CTL_RCV_DE 0x0800u  /* data error (crc_rs) */
#define AP_RING_CTL_RCV_PKE 0x0400u /* packet error (pkt_err/ackbyte_errbit) */
#define AP_RING_CTL_RCV_AKP 0x0200u /* ack parity error (ackparerr_rs) */
#define AP_RING_CTL_RCV_IFE 0x0100u /* interface error: overrun or controller */
#define AP_RING_CTL_RCV_NCT 0x0080u /* 0 => network connect */
#define AP_RING_CTL_RCV_REN 0x0040u /* 1 => receive enable */
#define AP_RING_CTL_RCV_RBY 0x0020u /* 0 => receive busy */
#define AP_RING_CTL_RCV_BPE 0x0010u /* 1 => bi-phase error (phs_rs) */
#define AP_RING_CTL_RCV_ESB 0x0008u /* elastic-store buffer error (esb_rs) */
/* 2:0 are the three receive counter outputs, one per 8254 counter -- see
 * `AP_RING_CTL_RCV_*_CNT` above. */
#define AP_RING_CTL_RCV_RC2 0x0004u /* packet exceeded max_rcv_cnt */
#define AP_RING_CTL_RCV_RC1 0x0002u /* data receive in progress */
#define AP_RING_CTL_RCV_RC0 0x0001u /* header receive in progress */

/* And `+404`'s idle word, from subtest 15: `(+404) & $F8 == $E0`, so bits 7-5
 * set with bits 4 and 3 clear. `E0` is exactly a disconnected,
 * receive-enabled, not-busy board with no errors, which is what a just-reset
 * one is. The firmware's constant and the manual's bit table agree without
 * either having been used to derive the other. */
#define AP_RING_CTL_COMMAND2_STATUS_IDLE 0x00E0u
/* The by-number spellings the model was written with, kept as aliases so the
 * findings that cite them still read, and so this change is a renaming rather
 * than a rewrite of behaviour that the firmware has already accepted. */
#define AP_RING_CTL_STATUS_BIT13 AP_RING_CTL_STATUS_XBY
#define AP_RING_CTL_STATUS_BIT11 AP_RING_CTL_STATUS_IOV
#define AP_RING_CTL_STATUS_BIT2 AP_RING_CTL_STATUS_XI
#define AP_RING_CTL_STATUS_BIT1 AP_RING_CTL_STATUS_RI
#define AP_RING_CTL_STATUS_BIT14 AP_RING_CTL_STATUS_TMO
#define AP_RING_CTL_STATUS_BIT3 AP_RING_CTL_STATUS_GPS

/* The dual-ported RAM, finding 46: `$7FFF + 1` words, which is 64 KB. The
 * figure is the firmware's own -- the extent it tests -- and not a datasheet's,
 * because there is no datasheet. */
#define AP_RING_CTL_BUFFER_WORDS 0x8000u

/* `[MAC]` §2.2.2.1's frame start **sequence**: the frame start character, a
 * null separator and a separator character -- three, which is what finding 87a
 * records and `ap_ring_station` already destuffs past. Named here because the
 * receive header counter's shortfall is exactly that many (`RING.md` 120). */
#define AP_RING_FRAME_START_CHARACTERS 3u

/* The early acknowledge a frame nobody copied comes back with: `[MAC]` Figure
 * 2-7's bit 1 alone, which is the parity bit making an otherwise empty field
 * odd. What the firmware's loopback reads at `+7` (`RING.md` 122). */
#define AP_RING_CTL_EARLY_ACK_UNCOPIED 0x0002u

/* ## The three command registers, `002398-04` p. 12-32
 *
 * The write halves of `59400`/`59402`/`59404`. **Two of the three are confirmed
 * on this board and the third is not**, and the split is evidence rather than
 * caution -- `RING.md` 103, from Domain/OS's own kernel ring driver
 * (`RING_PROC`, extracted from the installed volume).
 *
 * **MISC_CMD and RCV_CMD apply.** `RING_PROC` writes `$800` and `$900` to
 * MISC_CMD -- bit 11 `nct` alone, and `nct` with bit 8 `lpb` -- and `$800` to
 * RCV_CMD, its single bit 11 `rcv`. Both are p. 12-32's layout unshifted, and
 * the AT boot firmware's `move.b #$8,$404` is the same `$0800`. Two independent
 * drivers and the page agree.
 *
 * **XMIT_CMD's layout does not.** Both drivers write exactly `$0100`, `$0200`
 * and `$0600` to it -- the kernel as words, the AT firmware as the bytes `$1`,
 * `$2`, `$6` in the high lane -- and p. 12-32's only defined bits are 15, 14
 * and 13. The three values reproduce the page's *structure* (three functions,
 * with the third being the second OR'd with a higher bit, which is exactly its
 * "force transmit is a **modifier** to transmit enable, not a separate
 * command"), so `ine`/`ten`/`fen` at bits 8/9/10 is the obvious reading -- and
 * it is **not** asserted, because no source states this board's layout and its
 * two sibling registers are unshifted. `ap_ring_ctl.c` still treats the AT
 * command lane as the opaque byte both drivers write. */
#define AP_RING_CTL_MISC_CMD_BPM 0x1000u /* bad packet marking enable */
#define AP_RING_CTL_MISC_CMD_NCT 0x0800u /* network connect */
#define AP_RING_CTL_MISC_CMD_TD1 0x0400u /* txdiag1 -- diagnostics only */
#define AP_RING_CTL_MISC_CMD_TD2 0x0200u /* txdiag2 -- diagnostics only */
#define AP_RING_CTL_MISC_CMD_LPB 0x0100u /* digital loopback enable */

#define AP_RING_CTL_XMIT_CMD_FEN 0x8000u /* force transmit, modifies `ten` */
#define AP_RING_CTL_XMIT_CMD_TEN 0x4000u /* transmit enable; 0 aborts */
#define AP_RING_CTL_XMIT_CMD_INE 0x2000u /* initialize enable */

#define AP_RING_CTL_RCV_CMD_RCV 0x0800u /* receive enable; 0 aborts */

/* ## The **first** window's write side, from `002398-04` p. 12-29
 *
 * The page tabulates every ring register by bus address, physical address,
 * "When Read" and "When Written", and the first window's two banks are eight
 * **write-only** registers behind the node ID PROM. Nothing here was modelled:
 * `+000` and `+400`-`+406` were inert or generic storage, and `+006` was
 * actively wrong -- it set the RAM pointer, which is a *second*-window
 * register (`59006`), on a window where the manual puts `TIMO_ACK`.
 *
 * The AT board's own firmware corroborates the bank at `+400`. `$944`'s tail
 * (`r3500.lst` `0009BA`-`0009C6`) clears `d3` and writes it to `$4(a3)`,
 * `$400(a3)`, `$402(a3)` and `$406(a3)` -- RCV_ACK, then **exactly the three
 * clear registers**, skipping `$404`, the one *request* register in the bank.
 * A routine clearing receive state would touch precisely those four and no
 * others, so this is the DN3000 page and the AT firmware agreeing on the map
 * rather than the page being carried across on address arithmetic alone.
 *
 * `+002` and `+004` are XMIT_ACK and RCV_ACK, which findings 74 and 74a had
 * already recovered from the firmware alone; the page names them. */
#define AP_RING_CTL_W1_SOFT_RCV_REQ 0x000u
#define AP_RING_CTL_W1_XMIT_ACK 0x002u
#define AP_RING_CTL_W1_RCV_ACK 0x004u
#define AP_RING_CTL_W1_TIMO_ACK 0x006u
#define AP_RING_CTL_W1_ERR_BITS_CLR 0x400u
#define AP_RING_CTL_W1_GPS_CLR 0x402u
#define AP_RING_CTL_W1_SOFT_XMIT_REQ 0x404u
#define AP_RING_CTL_W1_LERR_CLR 0x406u

/* The sticky error bits `ERR_BITS_CLR` clears, named by MISC_STAT's own
 * diagram: `rlk` receive lock error, `esb` sticky elastic-store buffer error,
 * `bpe` sticky bi-phase error. "Sticky" is the manual's word and a latched
 * condition needs a clear; this is the register that supplies one, which is
 * what finding 53b predicted from `RING_$POLL_STICKY_BPHERR`'s name alone. */
#define AP_RING_CTL_STATUS_STICKY_ERRORS                                       \
  (AP_RING_CTL_STATUS_RLK | AP_RING_CTL_STATUS_ESB | AP_RING_CTL_STATUS_BPE)

/* ## And the second window's `+002`/`+004`, which were generic storage
 *
 * p. 12-29: `59002` is **XMIT_ADDR**, `59004` reads **XMIT_ABORT** and writes
 * **RCV_ADDR** on the single-board version. These are the buffer descriptors --
 * where in the 8K-word buffer a message begins -- and they are what finding
 * 50's loopback turns on: `$944` ends `move.w #$10,$4(a4)`, setting
 * **RCV_ADDR = $10**, and `$BAC` then reads the received frame back from
 * buffer word **16**. The firmware's magic `$10` and the manual's register name
 * explain each other, and neither was used to derive the other.
 *
 * p. 12-32 gives their layout, and it is **byte-swapped**: bits 15-8 carry
 * `a7`-`a0` and bits 7-0 carry `a15`-`a8`. Not applied here, because nothing
 * yet uses these as addresses -- when the transmit path does, it must swap. */
#define AP_RING_CTL_W2_XMIT_ADDR 0x002u
#define AP_RING_CTL_W2_RCV_ADDR 0x004u

/* The header a transmit command takes out of the buffer. `[MAC]` §2.2.2's
 * minimum, which is the only length that is *evidenced*: no document gives the
 * board a header-length field, and finding 49 shows the AT firmware writing
 * eight bytes into a cleared buffer and issuing the command. Twelve bytes is
 * what §2.2.2 permits at the short end -- destination, type, the zero byte and
 * the early acknowledge, and the four bytes after them. A longer header needs a
 * source for where its length comes from; see `RING.md` 104c. */
#define AP_RING_CTL_XMIT_HEADER_BYTES 12u
#define AP_RING_CTL_XMIT_HEADER_WORDS (AP_RING_CTL_XMIT_HEADER_BYTES / 2u)

/* Finding 39: the only two values init accepts. ASCII `'6'` and `'7'`, which
 * with `[ROM3500]`'s revision string ` 3.6` and `[ROM4500]`'s ` 4.0` looks like
 * a board revision -- but the ROM only ever compares, so that reading is not
 * evidenced and nothing here depends on it. */
#define AP_RING_CTL_ID_6 0x36u
#define AP_RING_CTL_ID_7 0x37u

/* One window's worth of state. */
typedef struct {
  /* `+000`, read as a byte by init and cleared as a word by the self-test. */
  uint8_t id;
  /* `+002` and `+004`, written by the self-test and by `$944`/`$976`. Storage:
   * `+004` is read once, at `000944`, and its value is discarded. */
  uint16_t slot_002;
  uint16_t slot_004;
  /* `+006`, the buffer pointer -- finding 46. In **words**, which is the unit
   * `+406` advances by. */
  uint16_t pointer;
  /* `+400` and the two command registers beside it, kept because the firmware
   * writes them and reads them back -- not because their bits are known. */
  uint16_t status;
  uint16_t command_402;

  /* `+402`'s low lane, which is status rather than the constant finding 63
   * first modelled: subtest 13 requires `F0` on an idle register and subtest 23
   * requires `B0` once a `$6` command has been taken, so bit 6 goes with the
   * operation. Held per window because it changes. */
  uint16_t command_402_status;
  /* `+404`'s low lane, the same shape and cleared by the same event. */
  uint16_t command_404_status;
  uint16_t command_404;

  /* Whether a `$6` command is awaiting its acknowledge. An acknowledge with
   * nothing outstanding must do nothing: subtest 16 runs after a `$2` command
   * and *requires bit 3 clear*, so a model that set it on every write to the
   * first window's `+2` fails there -- measured, not reasoned. `RING.md` 75. */
  bool operation_pending;
  /* MISC_CMD's `lpb`, digital loopback (p. 12-32 bit 8). It is what separates
   * a `$2` that starts an operation from one that does nothing -- `RING.md`
   * 123 -- and it is the *only* difference between the two sites that write
   * that command after a byte-identical preamble. */
  bool loopback_enabled;

  /* `+406` on the `a1` window, which finding 50a shows is never read. The `a2`
   * window's `+406` is the buffer port and does not use this. */
  uint16_t slot_406;

  /* **A 16-bit port reached over a byte bus needs a half-word latch.** The
   * board delivers a `move.w` as two byte accesses, and `+406` advances its
   * pointer on every access -- so reading it twice for one word consumes two
   * words of the buffer and hands the firmware halves of different ones. The
   * 8254s above already guard against exactly this; the data port did not.
   * The even half performs the access and latches, the odd half is served from
   * the latch. */
  uint16_t port_latch;
  uint16_t port_write_high;
  /* The one-word read-ahead latch of finding 46a. Its power-on content is
   * whatever the port last fetched, which is why the firmware discards its
   * first read rather than trusting it. */
  uint16_t read_ahead;
  ap_i8254_t timer_a;
  ap_i8254_t timer_b;
} ap_ring_ctl_window_t;

typedef struct {
  /* The board's own node ID, read back through the first window's four byte
   * slots most significant first (`[EH]` p. 12-29). */
  uint32_t node_id;
  /* A unit is the pair, finding 38. */
  ap_ring_ctl_window_t a1;
  ap_ring_ctl_window_t a2;
  /* The card's dual-ported RAM, reached through the `a2` window's `+406`.
   * `[S3K]` §1.5.4 gives the board one buffer, so it belongs to the card rather
   * than to a window. */
  uint16_t buffer[AP_RING_CTL_BUFFER_WORDS];
  /* Whether a board is fitted. Drives `+400` bit 15, which is the whole of what
   * the firmware uses to tell an empty slot from a populated one. */
  bool present;

  /* ## The wire to the ring, `RING.md` 104
   *
   * Until this existed the controller and the ring protocol stack were two
   * unconnected halves: `ap_ring_station` was referenced only by itself and
   * `ap_ring_probe`, so **no frame the Domain/OS driver queued could reach the
   * medium** and two nodes could not see each other however many machines were
   * run. `RING.md` 85e recorded it and it stayed open through the whole
   * `[MAC]` audit.
   *
   * Optional: a controller with no station attached behaves exactly as before,
   * which is what keeps the boot hash and the firmware self-test unchanged. */
  ap_ring_station_t *station;
  ap_ring_medium_t *medium;
  /* The station's copied-frame count as of the last deposit, so a frame lands
   * in the buffer once however often the ring is polled. */
  uint64_t rx_copied_seen;
} ap_ring_ctl_t;

/* Join a controller to a station on a medium. Both pointers are borrowed and
 * `src/core` allocates nothing, as everywhere else. */
void ap_ring_ctl_attach_ring(ap_ring_ctl_t *ctl, ap_ring_station_t *station,
                             ap_ring_medium_t *medium);

/* Take whatever the station has accepted since the last call: deposit it in the
 * buffer at `RCV_ADDR` and assert `ri`. Called from `ap_ring_ctl_clock`, and
 * separately callable so a test can drive the receive path without a tick. */
void ap_ring_ctl_poll_ring(ap_ring_ctl_t *ctl);

/* Whether the card is asserting its interrupt line.
 *
 * Asserted when `xi` or `ri` reads **clear**: p. 12-30 marks those two "intr
 * pending <=0", active low. `gps` is "sticky good pkt <=1" -- active *high*,
 * and a status bit rather than an interrupt -- and `tmi` is excluded because
 * the idle word leaves it clear, which under its own `<=0` notation would have
 * every card interrupt at power-on. See `RING.md` 110b. An unfitted card drives nothing, which is why the presence
 * gate is checked first rather than the bits: `RING.md` 40 makes an empty slot
 * a successful outcome for the firmware's probe, and a slot that interrupted
 * unbidden would take a machine with no ring hardware down a path it never
 * runs. */
[[nodiscard]] bool ap_ring_ctl_irq(const ap_ring_ctl_t *ctl);

/* Take whatever the station has accepted since the last call: deposit it in the
 * buffer at `RCV_ADDR` and assert `ri`. Called from `ap_ring_ctl_clock`, and
 * separately callable so a test can drive the receive path without a tick. */
void ap_ring_ctl_poll_ring(ap_ring_ctl_t *ctl);

/* Power-on. `present` chooses whether the unit answers as a fitted board. */
void ap_ring_ctl_reset(ap_ring_ctl_t *ctl, bool present);

/* **The board carries its own node ID, and the first window is where it
 * reads** -- `[EH]` p. 12-29 (`RING.md` 93): bus `220`-`226` read `Node_ID3`
 * (msb) through `Node_ID0` (lsb), while the *second* window's `+000` reads
 * `BOARD_TYPE`. This core answered the board type from both, which nothing
 * caught because finding 50a established the firmware never reads the first
 * window at all -- an unexercised register answering the wrong thing. */
void ap_ring_ctl_set_node_id(ap_ring_ctl_t *ctl, uint32_t node_id);

/* Whether an address falls in one of the four windows, and which half of which
 * unit. */
[[nodiscard]] bool ap_ring_ctl_decode(uint32_t address, unsigned *unit,
                                      bool *second_window, uint32_t *offset);

/* Byte access. The firmware reads the ID as a byte and the timers are
 * byte-wide, so a byte entry is the primitive and the word entries build on it.
 */
[[nodiscard]] uint8_t ap_ring_ctl_read8(ap_ring_ctl_t *ctl, bool second_window,
                                        uint32_t offset);
void ap_ring_ctl_write8(ap_ring_ctl_t *ctl, bool second_window,
                        uint32_t offset, uint8_t value);

/* Word access, big-endian as the bus is. */
[[nodiscard]] uint16_t ap_ring_ctl_read16(ap_ring_ctl_t *ctl,
                                          bool second_window, uint32_t offset);
void ap_ring_ctl_write16(ap_ring_ctl_t *ctl, bool second_window,
                         uint32_t offset, uint16_t value);

/* One clock pulse to both of a window's timers. */
void ap_ring_ctl_clock(ap_ring_ctl_t *ctl, bool second_window);

#endif /* APOLLO_DEVICE_AP_RING_CTL_H */
