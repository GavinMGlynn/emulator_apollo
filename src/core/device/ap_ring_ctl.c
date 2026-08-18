#include "device/ap_ring_ctl.h"
#include "board/ap_nodeid.h"

#include <string.h>

/* The XMIT_ADDR / RCV_ADDR / RAM_ADDR layout, `002398-04` p. 12-32: bits 15-8
 * carry `a7`-`a0` and bits 7-0 carry `a15`-`a8`. The two address bytes are
 * **swapped**, which is not a detail a model can skip -- taking the register at
 * face value puts every buffer access 256 words away from where the driver
 * meant, and the resulting frame is well-formed rubbish. */
static uint16_t ring_ctl_addr(uint16_t reg) {
  /* **Swapped, as the page draws it -- and the ROM computes the swap itself.**
   *
   * `RING.md` 104b asserted this from p. 12-32, 122a withdrew it on the
   * firmware's evidence, and 133 restores it: *the withdrawal's experiment
   * could not distinguish the two readings.* It rested on `$944` writing
   * `+004 = $10` and `$BAC` reading back at `+006 = $10` -- the **same value**
   * in both registers, which decodes alike swapped or not. A control that
   * varies neither variable measures nothing.
   *
   * What settles it is `$E08`, the ROM's own buffer-write helper, which halves
   * a byte address to words and then **`rol.w #$8`** before storing it to
   * `+006` (`$E22` does the same on the read side). A driver that must swap
   * the bytes it writes is writing to a register whose bytes are swapped, and
   * that is p. 12-32's diagram exactly.
   *
   * The self-test's own constants then decode into a layout that the flat
   * reading could not produce: `$0000` -> `$0000` and `$0600` -> `$0006` are
   * the transmit header and its data, `$0010` -> `$1000` and `$0610` ->
   * `$1006` the receive buffer and *its* data -- six words past the header,
   * which is exactly `AP_RING_CTL_XMIT_HEADER_WORDS`. Under the flat reading
   * `$600` and `$610` are sixteen words apart, the subtest 88 fill at `$600`
   * runs straight over the data at `$610`, and the firmware requires `FFFF`
   * from a region it has just zeroed. `RING.md` 132, 133. */
  return (uint16_t)(((reg & 0x00FFu) << 8) | (reg >> 8));
}

/* Assemble the frame sitting in the board's buffer and hand it to the station.
 *
 * The buffer layout is `[EH]` p. 12-29's: "7 rcv msg buffers (each **1k bytes
 * of header and 1k bytes of data**) and 1 xmit msg buffer of the same size",
 * addressed by `XMIT_ADDR`. So a message is a 1 KB header followed by 1 KB of
 * data, and `[MAC]` §2.2.2's rules -- header 12 to 1024 bytes and even, data 0
 * to 4096 and even -- are what `ap_ring_frame_emit` then checks.
 *
 * **How much of that 1 KB is the header is the board's business, not ours.**
 * Nothing in either document gives a length field, and finding 49 shows the AT
 * firmware writing eight bytes and leaving the rest of a cleared buffer alone.
 * So the minimum §2.2.2 allows is used, which is the only length that is
 * evidenced rather than chosen: 12 bytes. A longer header needs a source that
 * says where its length comes from, and is a named gap rather than a guess. */
static bool ring_ctl_queue_from_buffer(ap_ring_ctl_t *ctl) {
  const uint16_t base = ring_ctl_addr(ctl->a2.slot_002);
  if ((size_t)base + AP_RING_CTL_XMIT_HEADER_WORDS > AP_RING_CTL_BUFFER_WORDS) {
    return false;
  }
  uint8_t header[AP_RING_CTL_XMIT_HEADER_BYTES];
  for (unsigned i = 0; i < AP_RING_CTL_XMIT_HEADER_WORDS; i++) {
    const uint16_t word = ctl->buffer[base + i];
    header[i * 2u] = (uint8_t)(word >> 8);
    header[i * 2u + 1u] = (uint8_t)(word & 0xFFu);
  }
  const ap_ring_frame_fields_t fields = {
      .header = header,
      .header_bytes = sizeof header,
      .data = NULL,
      .data_bytes = 0u,
      .late_acknowledge = 0u,
  };
  if (!ap_ring_station_queue_frame(ctl->station, &fields)) {
    return false;
  }
  /* The previous frame's outcome is not this one's, and the station has already
   * dropped it. Both sides forget together or a driver reads a stale `cpd`. */
  ctl->tx_ack_seen = false;
  ctl->a2.xmit_status = (uint16_t)(
      ctl->a2.xmit_status & (uint16_t) ~(AP_RING_CTL_XMIT_CPD |
                                         AP_RING_CTL_XMIT_WAK |
                                         AP_RING_CTL_XMIT_ICP));
  /* The transmit trio, in `ring8a.drvr`'s units (`RING.md` 100): `XMT_HDR`
   * "Transmitter Header **Word**" and `XMT_PKT` "Transmitter Total **Word**".
   * Words, where the receive pair count bytes -- which is the asymmetry
   * finding 100a found and 80c was posed without. */
  for (unsigned i = 0; i < AP_RING_CTL_XMIT_HEADER_WORDS; i++) {
    ap_i8254_clock_counter(&ctl->a2.timer_b, AP_RING_CTL_XMIT_HDR_CNT);
    ap_i8254_clock_counter(&ctl->a2.timer_b, AP_RING_CTL_XMIT_PKT_CNT);
  }
  return true;
}

/* The gate array's internal transmit-to-receive DMA loop.
 *
 * The received image is `[MAC]` Figure 2-5's header, built rather than copied.
 * `$B70` writes four words at the transmit address -- destination high,
 * destination low, type, and an early-acknowledge word -- and `$BAC` reads
 * **six** back, requiring destination, type, `0002`, destination again
 * (`RING.md` 121). So two of the six are the card's to supply:
 *
 *   - the **early acknowledge** at `+7`, which comes back `0002`. Figure 2-7
 *     makes that bit 1 alone -- the parity bit, with intend-to-copy clear --
 *     which is a well-formed "nobody copied this" field, and is *not* what the
 *     firmware sent (`$0A` or `$0E`). The transmitter inserts this field and
 *     the receiver modifies it; in a loop with no addressed receiver, nothing
 *     sets intend-to-copy, and odd parity over an otherwise empty field is one
 *     bit. Both halves of that are the manual's.
 *
 *   - the **source address**, which comes back equal to the *destination*.
 *     That is established by the firmware's own walking-bit loop, not by one
 *     comparison: `0007CE` transmits with `d3 = d7` and `000862` compares
 *     expecting `d7` back, with `lsl.l #$1,d7` between iterations, so the
 *     field tracks a value that walks every bit position. It cannot be a fixed
 *     node ID and it cannot be stale buffer content, which would lag by one
 *     iteration. `RING.md` 122. */
static void ring_ctl_loopback(ap_ring_ctl_t *ctl) {
  const uint16_t from = ring_ctl_addr(ctl->a2.slot_002); /* XMIT_ADDR */
  const uint16_t to = ring_ctl_addr(ctl->a2.slot_004);   /* RCV_ADDR */
  if ((size_t)from + 3u > AP_RING_CTL_BUFFER_WORDS ||
      (size_t)to + 6u > AP_RING_CTL_BUFFER_WORDS) {
    return;
  }
  /* **Everything the transmit supplied is read before anything is written.**
   * `$B70` issues its command *before* `$944` sets `RCV_ADDR`, so on the first
   * pass `to` still equals `from` and a deposit made field by field overwrites
   * the very words it is about to read -- which is what made the early
   * acknowledge come back as the card's own `0002` and cost one wrong
   * conclusion about intend-to-copy. */
  const uint16_t dest_hi = ctl->buffer[from + 0u];
  const uint16_t dest_lo = ctl->buffer[from + 1u];
  const uint16_t type = ctl->buffer[from + 2u];
  const uint16_t early = ctl->buffer[from + 3u];
  ctl->buffer[to + 0u] = dest_hi;
  ctl->buffer[to + 1u] = dest_lo;
  ctl->buffer[to + 2u] = type;
  ctl->buffer[to + 3u] = AP_RING_CTL_EARLY_ACK_UNCOPIED;
  ctl->buffer[to + 4u] = dest_hi;
  ctl->buffer[to + 5u] = dest_lo;

  (void)early;
}

/* **The DMA loop's block shape.** The subtest 71-77 loop writes 1018 words at
 * buffer `$600`, issues a transmit with **no `$B70`**, and requires 1018 words
 * at `$610` to match (`RING.md` 127). `$610` is `$600 + $10`, and `$10` is
 * what `$976` puts in `RCV_ADDR`, so the destination is the source plus
 * `RCV_ADDR` -- which also holds for `$B70`'s case, source 0 and `$BAC`
 * reading at `$10`. Two call sites agree.
 *
 * The extent is `XMIT_PKT_CNT`'s loaded value, which `ring8a.drvr` names
 * "Transmitter **Total** Word" (finding 100). The source is where `RAM_ADDR`
 * was last *set*. The header build goes on top only for a `$1`-armed packet
 * transmit: subtest 41 needs the built fields, subtest 77 needs a clean copy. */
static void ring_ctl_block_move(ap_ring_ctl_t *ctl) {
  const uint16_t from = ctl->a2.pointer_base;
  const uint16_t off = ring_ctl_addr(ctl->a2.slot_004);
  const uint32_t count = ctl->a2.timer_b.counter[1].latch;
  const uint32_t to = (uint32_t)from + off;
  if (count == 0u || to + count > AP_RING_CTL_BUFFER_WORDS) {
    return;
  }
  /* Descending: source and destination overlap whenever the offset is smaller
   * than the extent -- `$10` against 1023 -- and a forward copy would smear
   * the first words across the block. */
  for (uint32_t i = count; i-- > 0u;) {
    ctl->buffer[to + i] = ctl->buffer[from + i];
  }
  if (ctl->a2.xmit_packet) {
    ring_ctl_loopback(ctl);
    ctl->a2.xmit_packet = false;
  }
}

bool ap_ring_ctl_irq(const ap_ring_ctl_t *ctl) {
  if (ctl == NULL || !ctl->present) {
    return false;
  }
  /* **`xi`, `ri` and `tmi`, but not `gps`.** p. 12-30's polarity notation is per
   * bit and is not uniform: `xi` and `ri` are "intr pending **<=0**" -- active
   * low, which is finding 93b's reading of the `$6` command -- but `gps` is
   * "sticky good pkt **<=1**", active *high* and not an interrupt at all. A
   * first version took all four as active low and every fitted card asserted
   * its line at reset, because the idle word `F806` has `gps` and `tmi` clear.
   *
   * `tmi` was excluded while its idle state was unknown -- the firmware's
   * subtest 01 masks with `$F806` and never constrains bit 0 -- and is included
   * now that `RING_PROC` has settled it: `7A4D0944` branches past its error
   * call when the bit is **set**, so a healthy board reads 1 and clear is the
   * pending timeout (`RING.md` 111). The idle word carries it accordingly. */
  const uint16_t pending =
      (uint16_t)(AP_RING_CTL_STATUS_RI | AP_RING_CTL_STATUS_XI |
                 AP_RING_CTL_STATUS_TMI);
  return (ctl->a2.status & pending) != pending;
}

void ap_ring_ctl_poll_ring(ap_ring_ctl_t *ctl) {
  if (ctl == NULL || ctl->station == NULL) {
    return;
  }
  /* **`[MAC]` §2.2.2.5's read-back, folded into XMIT_STAT.** The frame this
   * node sent has been round the ring and back; the late acknowledge it carries
   * was written by the receivers it passed, and reading it is the only way a
   * sender ever learns whether anybody took the packet. p. 12-31 names where it
   * lands: `cpd` at bit 14, `wak` at 13, `icp` at 12.
   *
   * **Only those three are mapped, because only those three are named the same
   * in both documents.** Figure 2-8's remaining bit is "an error was observed,
   * *or* the sender aborted", and p. 12-31 offers `pke` (packet error) and
   * `abt` (packet aborted) as separate destinations -- nothing in either source
   * says which, so it is left unmapped and recorded rather than fitted.
   * `RING.md` 137b. */
  {
    uint8_t ack = 0u;
    if (!ctl->tx_ack_seen &&
        ap_ring_station_transmit_ack(ctl->station, &ack)) {
      ctl->tx_ack_seen = true;
      ctl->a2.xmit_status = (uint16_t)(
          ctl->a2.xmit_status &
          (uint16_t) ~(AP_RING_CTL_XMIT_CPD | AP_RING_CTL_XMIT_WAK |
                       AP_RING_CTL_XMIT_ICP));
      if ((ack & AP_RING_LATE_COPIED) != 0u) {
        ctl->a2.xmit_status |= AP_RING_CTL_XMIT_CPD;
      }
      if ((ack & AP_RING_LATE_WAIT_ACK) != 0u) {
        ctl->a2.xmit_status |= AP_RING_CTL_XMIT_WAK;
      }
      if ((ack & AP_RING_LATE_INTEND_TO_COPY) != 0u) {
        ctl->a2.xmit_status |= AP_RING_CTL_XMIT_ICP;
      }
    }
  }

  /* One deposit per frame the station *copied*, which is its own count of
   * §2.2.2.2 acceptances -- addressed to this node, or broadcast, with the
   * receiver enabled. Edge-triggered on that counter rather than on a level,
   * so a frame is deposited once however often this is polled. */
  if (ctl->station->frames_copied == ctl->rx_copied_seen) {
    return;
  }
  ctl->rx_copied_seen = ctl->station->frames_copied;
  /* p. 12-30 bit 14: this station copied the packet. The receive counterpart of
   * the transmit read-back above, and the other half of what a driver needs to
   * see a frame arrive rather than infer it from an interrupt. */
  ctl->a2.rcv_status |= AP_RING_CTL_RCV_CPD;

  /* **Where it lands is `RCV_ADDR`, and how much is the firmware's own
   * answer.** `002398-04` p. 12-29 gives `59004` as `RCV_ADDR` on write, and
   * `$944` sets it to `$10` (finding 98d). Finding 50's loopback then does
   * `+006 = $10` and reads **four words** back through `+406`, reassembles a
   * long and compares it against what it transmitted -- so eight bytes at that
   * address is exactly what the board's own diagnostic expects to find, and it
   * is exactly what §2.2.2.2 makes the station capture (`rx_header[8]`,
   * through the early acknowledge at `+7`).
   *
   * A frame longer than its first eight bytes is **not** deposited, because
   * the station does not capture one: §2.2.2.2's receive *decision* needs six
   * bytes and the station stops there rather than parsing a frame it is only
   * forwarding (finding 87a). Capturing a whole frame is a station change, and
   * a named gap -- `RING.md` 105b. */
  const uint16_t base = ring_ctl_addr(ctl->a2.slot_004);
  const unsigned words = sizeof ctl->station->rx_header / 2u;
  if ((size_t)base + words > AP_RING_CTL_BUFFER_WORDS) {
    return;
  }
  for (unsigned i = 0; i < words; i++) {
    ctl->buffer[base + i] =
        (uint16_t)((ctl->station->rx_header[i * 2u] << 8) |
                   ctl->station->rx_header[i * 2u + 1u]);
  }
  /* `ri` is MISC_STAT bit 1, "RCV intr pending **<=0**" (p. 12-30) -- active
   * low, so a pending interrupt *clears* it. Finding 74a already had the other
   * half: writing `RCV_ACK` at the first window's `+4` sets it again. The two
   * directions now belong to the two events rather than one of them being a
   * bare acknowledge with nothing to acknowledge. */
  ctl->a2.status &= (uint16_t)~AP_RING_CTL_STATUS_RI;

  /* **The 8254s clocked from real ring traffic**, in the units the board's own
   * driver names them with (`RING.md` 100, 108): `RCV_HDR` counts header
   * **bytes**, `RCV_DAT` data **bytes**, `RCV_MAX` **words**. The station takes
   * the header/data split at the second separator, which is the only point in
   * the stream where §2.2.2.2's boundary is observable.
   *
   * This is the wire findings 41a, 80c and 100c all named and none supplied:
   * an event on the medium advancing a counter, rather than a count injected
   * by the command path. It does **not** close 80c -- that question is about
   * the *internal DMA loopback*, where no frame crosses a medium at all
   * (79a) -- and the two paths are deliberately separate, because a model that
   * fed loopback traffic through here would be inventing a medium the
   * diagnostic does not use. */
  const size_t header = ctl->station->rx_header_bytes;
  const size_t total = ctl->station->rx_bytes;
  const size_t data = total > header ? total - header : 0u;
  for (size_t i = 0; i < header; i++) {
    ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_HDR_CNT);
  }
  for (size_t i = 0; i < data; i++) {
    ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_PKT_CNT);
  }
  for (size_t i = 0; i < total / 2u; i++) {
    ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_MAX_CNT);
  }
}

void ap_ring_ctl_attach_ring(ap_ring_ctl_t *ctl, ap_ring_station_t *station,
                             ap_ring_medium_t *medium) {
  if (ctl == NULL) {
    return;
  }
  ctl->station = station;
  ctl->medium = medium;
  /* The board's node ID *is* the station's ring address. They were two
   * unrelated numbers while the halves were unconnected -- `[MAC]` §2.2.2.2's
   * comparison is against "the node address of the target", and finding 93i
   * had already made the controller answer the board's node from the ID PROM.
   * Attaching is where the one number reaches both. */
  if (station != NULL) {
    ap_ring_station_set_address(station, ctl->node_id);
  }
  /* **And the relay comes up de-energised, which is `[MAC]` §3.5 read
   * literally**: "when powered *off* or under command of the controller,
   * relays connect a node's input coaxial cable to its output coaxial cable".
   * Powered off is the first clause, and a card that has just been plugged
   * into a segment has not yet run the firmware that would energise anything.
   *
   * It was in-ring, because `ap_ring_medium_attach` leaves a fresh slot with
   * `bypassed` false. That made a card a retiming element before its
   * controller had said a word -- and on real hardware it is the failure the
   * relay exists to prevent, an unpowered workstation breaking the ring for
   * everyone else.
   *
   * Set here rather than in `ap_ring_medium_attach` because the relay is the
   * *controller's*, not the cable's: a medium built directly by a test is a
   * piece of coax with no card behind it, and giving it an opinion about
   * bypass would be modelling a card that is not there.
   *
   * The firmware energises it almost at once -- MISC_CMD's `nct`, which the
   * boot PROM writes as `$800` and subtest 11 as `move.b #$1,$400` -- so a
   * board driven by firmware reaches the same state it always did. */
  if (medium != NULL && station != NULL) {
    ap_ring_medium_set_bypass(medium, station->slot, true);
  }
}

/* The just-reset register state, shared by power-on and by `BOARD_RESET`.
 * Identity and attachment are *not* touched: a board reset does not change the
 * node's address, unsolder its ID PROM or unplug it from the cable. */
static void ring_ctl_reset_registers(ap_ring_ctl_t *ctl) {
  ap_i8254_reset(&ctl->a1.timer_a);
  ap_i8254_reset(&ctl->a1.timer_b);
  ap_i8254_reset(&ctl->a2.timer_a);
  ap_i8254_reset(&ctl->a2.timer_b);

  ctl->a1.slot_002 = 0u;
  ctl->a2.slot_002 = 0u;
  ctl->a1.slot_004 = 0u;
  ctl->a2.slot_004 = 0u;
  ctl->a1.slot_406 = 0u;
  ctl->a2.slot_406 = 0u;
  ctl->a1.pointer = 0u;
  ctl->a2.pointer = 0u;
  ctl->a1.read_ahead = 0u;
  ctl->a2.read_ahead = 0u;
  ctl->a1.port_latch = 0u;
  ctl->a2.port_latch = 0u;
  ctl->a1.port_write_high = 0u;
  ctl->a2.port_write_high = 0u;
  ctl->a1.command_402 = 0u;
  ctl->a2.command_402 = 0u;
  ctl->a1.command_404 = 0u;
  ctl->a2.command_404 = 0u;
  ctl->a1.connected = false;
  ctl->a2.connected = false;
  ctl->a2.operation_pending = false;

  if (!ctl->present) {
    ctl->a1.status = 0u;
    ctl->a2.status = 0u;
    return;
  }
  /* Finding 39: init accepts `$36` or `$37` and nothing else. `[ROM3500]` is
   * the Apollo 10666 board, so this unit answers as one; a model that answered
   * `$37` would be equally consistent with the ROM, and the choice between them
   * is not evidenced -- recorded here rather than hidden, since the firmware
   * only ever compares. */
  ctl->a1.id = AP_RING_CTL_ID_6;
  ctl->a2.id = AP_RING_CTL_ID_6;
  /* Finding 40: bit 15 is what init reads to decide the slot is populated. */
  /* **The idle value the firmware's own self-test asserts.** Subtest 01 reads
   * `+400`, masks with `$F806` and requires the result to *equal* `$F806`
   * (`$A6E`: `move.w (a1),d1 / and.w d4,d1 / cmp.w d1,d2` with `d2 = d4 =
   * $F806`), failing to `loc_08D2` with code `E0000001` otherwise. So bits 15,
   * 14, 13, 12, 11, 2 and 1 all read set on a healthy board that has just been
   * reset.
   *
   * This is the firmware specifying its own hardware, which is the strongest
   * source this controller has -- there is no register manual for the AT board,
   * five documentary and cross-reading attempts failed to settle these bits,
   * and the ROM asserts them directly. It is *not* fitting the model to the
   * test: the assertion is what a working board reads, and the later subtests
   * constrain the same register further rather than agreeing with this one by
   * construction. `AP_RING_CTL_STATUS_PRESENT` is bit 15 of it, finding 40. */
  ctl->a1.status = AP_RING_CTL_STATUS_IDLE;
  ctl->a2.status = AP_RING_CTL_STATUS_IDLE;
  ctl->a1.command_402_status = AP_RING_CTL_COMMAND_STATUS_IDLE;
  ctl->a2.command_402_status = AP_RING_CTL_COMMAND_STATUS_IDLE;
  ctl->a1.command_404_status = AP_RING_CTL_COMMAND2_STATUS_IDLE;
  ctl->a2.command_404_status = AP_RING_CTL_COMMAND2_STATUS_IDLE;
}

void ap_ring_ctl_board_reset(ap_ring_ctl_t *ctl) {
  if (ctl == NULL) {
    return;
  }
  memset(ctl->buffer, 0, sizeof ctl->buffer);
  ring_ctl_reset_registers(ctl);
}

void ap_ring_ctl_reset(ap_ring_ctl_t *ctl, bool present) {
  if (ctl == NULL) {
    return;
  }
  /* **This is the initialiser as well as the reset**, and the distinction cost
   * a segfault: preserving `station`/`medium` across the `memset` -- so that
   * resetting a board would not unplug it from the ring -- reads them out of a
   * caller's *uninitialised* stack local on the first call, and the garbage
   * pointer is then dereferenced by the first `MISC_CMD` write. Every existing
   * caller uses this as init, so it clears the attachment and
   * `ap_ring_ctl_attach_ring` is called after it, not before. */
  memset(ctl, 0, sizeof *ctl);
  ctl->present = present;

  /* `a1` shares the window type but is PROM, so these two are inert. They are
   * still reset, because a shared struct with one half left to whatever `init`
   * memset it to is the sort of thing a state hash notices later. */
  ap_i8254_reset(&ctl->a1.timer_a);
  ap_i8254_reset(&ctl->a1.timer_b);
  ap_i8254_reset(&ctl->a2.timer_a);
  ap_i8254_reset(&ctl->a2.timer_b);

  if (!present) {
    return;
  }
  /* Finding 39: init accepts `$36` or `$37` and nothing else. `[ROM3500]` is
   * the Apollo 10666 board, so this unit answers as one; a model that answered
   * `$37` would be equally consistent with the ROM, and the choice between them
   * is not evidenced -- recorded here rather than hidden, since the firmware
   * only ever compares. */
  ctl->a1.id = AP_RING_CTL_ID_6;
  ctl->a2.id = AP_RING_CTL_ID_6;
  /* Finding 40: bit 15 is what init reads to decide the slot is populated. */
  /* **The idle value the firmware's own self-test asserts.** Subtest 01 reads
   * `+400`, masks with `$F806` and requires the result to *equal* `$F806`
   * (`$A6E`: `move.w (a1),d1 / and.w d4,d1 / cmp.w d1,d2` with `d2 = d4 =
   * $F806`), failing to `loc_08D2` with code `E0000001` otherwise. So bits 15,
   * 14, 13, 12, 11, 2 and 1 all read set on a healthy board that has just been
   * reset.
   *
   * This is the firmware specifying its own hardware, which is the strongest
   * source this controller has -- there is no register manual for the AT board,
   * five documentary and cross-reading attempts failed to settle these bits,
   * and the ROM asserts them directly. It is *not* fitting the model to the
   * test: the assertion is what a working board reads, and the later subtests
   * constrain the same register further rather than agreeing with this one by
   * construction. `AP_RING_CTL_STATUS_PRESENT` is bit 15 of it, finding 40. */
  ctl->a1.status = AP_RING_CTL_STATUS_IDLE;
  ctl->a2.status = AP_RING_CTL_STATUS_IDLE;
  ctl->a1.command_402_status = AP_RING_CTL_COMMAND_STATUS_IDLE;
  ctl->a2.command_402_status = AP_RING_CTL_COMMAND_STATUS_IDLE;
  ctl->a1.command_404_status = AP_RING_CTL_COMMAND2_STATUS_IDLE;
  ctl->a2.command_404_status = AP_RING_CTL_COMMAND2_STATUS_IDLE;
}

void ap_ring_ctl_set_node_id(ap_ring_ctl_t *ctl, uint32_t node_id) {
  if (ctl != NULL) {
    ctl->node_id = node_id;
  }
}

/* One byte of the node ID in the high lane -- this board's convention for
 * every byte-wide register -- with the odd half undriven. */
/* **The whole first window is the node ID PROM, and this core already models
 * that part.** `002398-04` p. 12-29 tabulates all sixteen of its slots at
 * stride two -- `Node_ID3` (msb) through `Node_ID0` at `51000`-`51006`,
 * `Node_ID_CHECKSUM` at `51C06`, and **"(unused - PROM)" at the eleven
 * between** -- with a `-` under "When Written" for the `51800` and `51C00`
 * banks, which have no write side at all.
 *
 * `roms/firmware/3500_NI_1C874.bin` is that PROM, and it reads
 *
 *     0000 0100 c800 7400 0000 0000 0000 0000
 *     0000 0000 0000 0000 0000 0000 0000 3d00
 *
 * -- sixteen big-endian words, each carrying its byte in the **high** half and
 * zero in the low one; `0001C874` in the first four (the file's own name); and
 * `00 + 01 + C8 + 74 = 0x13D`, whose low byte is the `3D` in the last. Which is
 * `ap_nodeid`'s layout exactly, derived independently from `011200`'s dump and
 * from self-test 8's disassembly at `008218`. **One part, and it had two
 * models here** -- so this window now serves the one that is right.
 *
 * What the second model had wrong, none of it reachable by the firmware
 * (finding 50a) and so none of it caught by a boot:
 *
 *   - the twelve PROM slots answered a status register and **two 8254s that
 *     this board does not have** -- p. 12-29 puts its only counters in the
 *     *second* window's `59800`/`59C00` banks, where they are modelled and
 *     driven;
 *   - `Node_ID_CHECKSUM` was absent, reading timer B's control word;
 *   - the low half of each ID lane answered `FF` where the PROM holds `00`,
 *     and `read8` and `read16` disagreed about it;
 *   - and `read8` of `+000` still returned the **board type**, which is
 *     finding 93i's defect surviving in the path 93i did not touch.
 */
static uint16_t ring_ctl_prom_word(const ap_ring_ctl_t *ctl, uint32_t offset) {
  if (!ctl->present) {
    /* An unpopulated slot leaves the bus to the pull-ups, as everywhere else
     * in this window. */
    return 0xFFFFu;
  }
  ap_nodeid_t prom;
  ap_nodeid_init(&prom, ctl->node_id);
  /* Four banks of four slots, in address order: bank picks the group of four,
   * the slot picks within it, so `51C06` is register 15 and `51000` is 0. */
  const unsigned reg = (((offset & AP_RING_CTL_BANK_MASK) >> 10) << 2) |
                       ((offset & AP_RING_CTL_SLOT_MASK) >> 1);
  const uint8_t byte =
      ap_nodeid_read(&prom, AP_NODEID_ADDR + (uint32_t)(reg << 1));
  return (uint16_t)((uint16_t)byte << 8);
}

bool ap_ring_ctl_decode(uint32_t address, unsigned *unit, bool *second_window,
                        uint32_t *offset) {
  static const struct {
    uint32_t base;
    unsigned unit;
    bool second;
  } windows[] = {
      {AP_RING_CTL_UNIT0_A1, 0u, false},
      {AP_RING_CTL_UNIT0_A2, 0u, true},
      {AP_RING_CTL_UNIT1_A1, 1u, false},
      {AP_RING_CTL_UNIT1_A2, 1u, true},
  };
  for (unsigned i = 0; i < sizeof windows / sizeof windows[0]; i++) {
    if (address >= windows[i].base &&
        address < windows[i].base + AP_RING_CTL_WINDOW) {
      if (unit != NULL) {
        *unit = windows[i].unit;
      }
      if (second_window != NULL) {
        *second_window = windows[i].second;
      }
      if (offset != NULL) {
        *offset = address - windows[i].base;
      }
      return true;
    }
  }
  return false;
}

static ap_ring_ctl_window_t *window_of(ap_ring_ctl_t *ctl, bool second) {
  return second ? &ctl->a2 : &ctl->a1;
}

/* A timer bank's four slots are the 8254's three counters and its control
 * word, at `+0`, `+2`, `+4` and `+6` -- finding 41. The part is byte-wide, and
 * the firmware's LSB-then-MSB helper writes bytes, so the slot maps straight
 * onto a register number. */
static ap_i8254_reg_t timer_reg(uint32_t offset) {
  return (ap_i8254_reg_t)((offset & AP_RING_CTL_SLOT_MASK) >> 1);
}

uint8_t ap_ring_ctl_read8(ap_ring_ctl_t *ctl, bool second_window,
                          uint32_t offset) {
  if (ctl == NULL) {
    return 0xFFu;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  /* The board's registers are byte-wide and sit at *even* offsets: findings 12
   * and 14 give a stride of two across four slots, and finding 41's `btst #14`
   * on a word read of `+802` puts the register byte in the word's high half,
   * which is the even address on a big-endian bus. So an odd byte is not a
   * register at all -- it is the other half of the lane, which nothing drives.
   */
  const bool odd = (offset & 1u) != 0u;

  /* The first window is PROM in all four banks -- see `ring_ctl_prom_word` --
   * so it is answered ahead of the decode below, which describes the second
   * window only. The odd half of every lane reads zero, which the PROM image
   * shows directly and which this path previously answered `FF`. */
  if (!second_window) {
    if (odd) {
      return ctl->present ? 0x00u : 0xFFu;
    }
    return (uint8_t)(ring_ctl_prom_word(ctl, offset) >> 8);
  }

  switch (offset & AP_RING_CTL_BANK_MASK) {
  case AP_RING_CTL_BANK_ID:
    /* Finding 39 reads `+000` as a byte. An unpopulated slot leaves the bus to
     * the pull-ups, which is `FF` on this machine -- the same reasoning the AT
     * window at large uses -- and `FF` is neither `$36` nor `$37`, so an absent
     * board fails the ID check exactly as it should. */
    if ((offset & AP_RING_CTL_SLOT_MASK) == 0u) {
      if (odd) {
        /* **The ID's low lane, and the firmware constrains one bit of it.**
         * Finding 39 reads `+000` as a *byte*, so the odd half never mattered
         * and answered with the pull-ups. Subtest 03 reads the same address as
         * a **word** -- `move.w (a4),d1 / andi.w #$8,d1` -- and requires the
         * result to be zero, which `FF` in this lane cannot give.
         *
         * So bit 3 reads clear on a healthy board. The rest of the lane is
         * unconstrained by anything measured, and zero is the least invented
         * answer: it asserts nothing the firmware did not, where `F7` would
         * claim six pull-ups this project has never seen. */
        return ctl->present ? 0x00u : 0xFFu;
      }
      return ctl->present ? w->id : 0xFFu;
    }
    if (odd) {
      return 0xFFu;
    }
    return (uint8_t)(ap_ring_ctl_read16(ctl, second_window, offset) >> 8);

  case AP_RING_CTL_BANK_STATUS: {
    /* The data port is the one slot in this bank with a side effect, so it is
     * the one that must be read once per word rather than once per byte. */
    if (second_window && (offset & AP_RING_CTL_SLOT_MASK) == 6u) {
      if (!odd) {
        w->port_latch = ap_ring_ctl_read16(ctl, second_window, offset & ~1u);
        return (uint8_t)(w->port_latch >> 8);
      }
      return (uint8_t)(w->port_latch & 0xFFu);
    }
    const uint16_t value = ap_ring_ctl_read16(ctl, second_window,
                                              offset & ~1u);
    return (offset & 1u) != 0u ? (uint8_t)(value & 0xFFu)
                               : (uint8_t)(value >> 8);
  }

  case AP_RING_CTL_BANK_TIMER_A:
    /* An 8254 read has side effects -- it unlatches, and it advances the
     * LSB/MSB cursor -- so the odd half of a word access must *not* reach the
     * part. A model that read the device for both halves would consume two
     * bytes of a two-byte count per `move.w` and hand the firmware the LSB and
     * MSB of the same read in the wrong halves. */
    return odd ? 0xFFu : ap_i8254_read(&w->timer_a, timer_reg(offset));
  case AP_RING_CTL_BANK_TIMER_B:
    return odd ? 0xFFu : ap_i8254_read(&w->timer_b, timer_reg(offset));
  default:
    break;
  }
  return 0xFFu;
}

void ap_ring_ctl_write8(ap_ring_ctl_t *ctl, bool second_window, uint32_t offset,
                        uint8_t value) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);
  /* The same even-address lane as the read side. */
  const bool odd = (offset & 1u) != 0u;

  switch (offset & AP_RING_CTL_BANK_MASK) {
  case AP_RING_CTL_BANK_ID:
    /* The ID at slot 0 absorbs its clear -- not host-writable.
     *
     * **The three word registers beside it assemble BOTH bytes**, which is
     * finding 61's rule for the data port applied where it also belongs: the
     * board "delivers a `move.w` as two byte accesses", so the even half holds
     * the high byte and the odd half commits the word. Taking the even byte
     * alone dropped the low half of every address the firmware writes --
     * `RAM_ADDR = $610` became `$600`.
     *
     * **That bug was load-bearing**: the firmware writes its buffer patterns
     * at `$600` and reads them back at `$610`, so collapsing both made every
     * read find what the write left, and the buffer checks passed by aliasing
     * with no DMA copy happening. It only lands beside the block move below.
     * `RING.md` 129a, 130. */
    if ((offset & AP_RING_CTL_SLOT_MASK) == 0u) {
      return;
    }
    if (!odd) {
      w->byte_latch = value;
      return;
    }
    ap_ring_ctl_write16(ctl, second_window, offset & ~1u,
                        (uint16_t)(((uint16_t)w->byte_latch << 8) | value));
    return;
  case AP_RING_CTL_BANK_STATUS: {
    /* The data port again, and the write side is the worse of the two: the
     * read-modify-write below would *read* the port -- advancing its pointer --
     * and then write it, twice over for one `move.w`, so a single word cost
     * four advances. The even half holds its byte and the odd half commits the
     * pair, which is what a 16-bit port on a byte bus does. */
    if (second_window && (offset & AP_RING_CTL_SLOT_MASK) == 6u) {
      if (!odd) {
        w->port_write_high = value;
        return;
      }
      ap_ring_ctl_write16(
          ctl, second_window, offset & ~1u,
          (uint16_t)(((uint16_t)w->port_write_high << 8) | value));
      return;
    }
    const uint16_t held = ap_ring_ctl_read16(ctl, second_window, offset & ~1u);
    const uint16_t merged =
        (offset & 1u) != 0u
            ? (uint16_t)((held & 0xFF00u) | value)
            : (uint16_t)((held & 0x00FFu) | (uint16_t)(value << 8));
    ap_ring_ctl_write16(ctl, second_window, offset & ~1u, merged);
    return;
  }
  /* Both counter banks belong to the **second** window: p. 12-29 gives the
   * first window's `51800` and `51C00` a bare `-` under "When Written", the
   * only registers on the board with no write side at all, because that window
   * is PROM. This core had an 8254 pair behind each window and clocked all
   * four. */
  case AP_RING_CTL_BANK_TIMER_A:
    if (!odd && second_window) {
      ap_i8254_write(&w->timer_a, timer_reg(offset), value);
    }
    return;
  case AP_RING_CTL_BANK_TIMER_B:
    if (!odd && second_window) {
      ap_i8254_write(&w->timer_b, timer_reg(offset), value);
    }
    return;
  default:
    break;
  }
}

uint16_t ap_ring_ctl_read16(ap_ring_ctl_t *ctl, bool second_window,
                            uint32_t offset) {
  if (ctl == NULL) {
    return 0xFFFFu;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  /* PROM, in all four banks, for the same reason `read8` answers it first. */
  if (!second_window) {
    return ring_ctl_prom_word(ctl, offset);
  }

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_ID) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      {
        /* `BOARD_TYPE`. The node ID that `[EH]` p. 12-29 puts at bus `220` is
         * the *first* window's answer and is served from the PROM above; this
         * is `59000`, and the two were once the same wrong value (finding 93). */
        const uint16_t byte = ctl->present ? w->id : 0xFFu;
        /* The odd half of the lane is undriven, as everywhere else on this
         * board. Finding 15's `movea.l (a2),a0` reads a long here and this is
         * what its second byte would be. */
        return (uint16_t)((uint16_t)(byte << 8) | 0x00FFu);
      }
    case 2u:
      /* `XMIT_ADDR`, which this core stores and does not yet act on. */
      return w->slot_002;
    case 4u:
      /* `XMIT_ABORT` on the two-board version, `RCV_ADDR` on the single-board
       * one -- p. 12-29 note *2. */
      return w->slot_004;
    default:
      /* `+006` is `RAM_ADDR`, the buffer pointer -- the firmware only ever
       * writes it, so a read-back is the least-surprising answer.
       *
       * Re-encoded, because the pointer is held decoded: the swap is its own
       * inverse, so a read taken before any access returns what was written. */
      return ring_ctl_addr(w->pointer);
    }
  }

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* Finding 40's presence gate. With no board the bit is clear, and init
       * returns success having touched nothing else -- an empty slot is not an
       * error. */
      return ctl->present ? w->status : 0u;
    case 2u:
      /* **The command byte is the high lane; the low lane is status.**
       * Finding 48 established that `+402` and `+404` are byte-wide command
       * registers that "carry status as well as command", and subtest 13 says
       * what the status half reads: the firmware writes `#$2` to `+402` and
       * then requires `(+402) & $F0 == $F0`, which a stored `0200` cannot give.
       * So bits 7-4 of the low lane read set on a healthy board.
       *
       * Only those four bits are asserted by anything measured, so only those
       * are answered -- the same restraint as the ID lane in finding 62. */
      /* p. 12-31's XMIT_STS: `pe` and the seven bits it selects in the high
       * byte, `nct`/`xen`/`iby`/`xby` and the transmit tags in the low. The
       * high byte was an echo of the written command, which no source
       * describes; it satisfied the ROM only because the ROM wants that half
       * to read **zero** at the one point it checks it (subtest 23's `$00B0`,
       * after an internal loopback with no addressed receiver -- which is
       * exactly what real status reads there too). */
      return (uint16_t)((w->xmit_status & 0xFF00u) | w->command_402_status);
    case 4u:
      /* The same shape one register along: subtest 15 requires
       * `(+404) & $F8 == $E0` after the firmware has written only the command
       * lane, so bits 7-5 read set and bits 4-3 clear. `+402`'s four bits and
       * these three are the whole of what the ROM asserts about either status
       * half; nothing else is answered. */
      /* p. 12-30's RCV_STS, high byte, on the same reasoning as `+402`. */
      return (uint16_t)((w->rcv_status & 0xFF00u) | w->command_404_status);
    default:
      /* `+406` is `RAM_DATA`, the buffer's data port -- finding 46a's
       * read-ahead latch, which answers with the word the *previous* access
       * fetched and then fetches the next. */
      {
        const uint16_t answered = w->read_ahead;
        w->read_ahead = w->pointer < AP_RING_CTL_BUFFER_WORDS
                            ? ctl->buffer[w->pointer]
                            : 0xFFFFu;
        w->pointer++;
        return answered;
      }
    }
  }

  /* Everywhere else a word is the two bytes, big-endian as the bus is. */
  const uint8_t high = ap_ring_ctl_read8(ctl, second_window, offset);
  const uint8_t low = ap_ring_ctl_read8(ctl, second_window, offset | 1u);
  return (uint16_t)(((uint16_t)high << 8) | low);
}

void ap_ring_ctl_write16(ap_ring_ctl_t *ctl, bool second_window,
                         uint32_t offset, uint16_t value) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_ID) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* **`BOARD_RESET`.** p. 12-29 gives `59000` as `BOARD_TYPE` when read and
       * `BOARD_RESET` when written, and this absorbed the write on the reading
       * that the type is "not host-writable" -- true, and not the whole entry.
       * The first window's `51000` is PROM and takes no write at all. */
      if (second_window) {
        ap_ring_ctl_board_reset(ctl);
      }
      return;
    case 2u:
      w->slot_002 = value;
      /* **The first window's `+2` acknowledges bit 2, and the firmware says
       * so by which helper writes it.** `RING.md` 74: `$9FA` polls `+400`
       * bit **2** and ends `move.w #$0,$2(a3)`; `$A28` polls bit **1** and
       * ends `move.w #$0,$4(a3)`; `$9D2`, which polls bit 13, writes nothing.
       * Two helpers, two addresses, and each acknowledges the bit it just
       * waited on -- so the bit returns when its condition is acknowledged,
       * with no duration anywhere.
       *
       * This also corrects finding 69, whose premise was that there is "no
       * write to any ring register" between subtests 22 and 26. There are two,
       * in the *first* window, and 56b had already recorded them. */
      if (!second_window && ctl->a2.operation_pending) {
        /* **Bits 13 and 3 come back here too, and the choice of *which*
         * acknowledge is unobservable rather than fitted.** Bit 13's own
         * helper `$9D2` writes nothing and bit 3 has no helper at all, so both
         * must be restored by one of the two acknowledges -- and nothing reads
         * `+400` between them: subtests 23 and 85 read `+402`, 25 reads `+404`,
         * and `$A56`/`$A62` write nothing. So attaching them to `+2` or to
         * `+4` produces **identical firmware-visible behaviour at all four
         * `$6` sites**, including `$7F4`'s, which never polls bit 13 at all and
         * still requires it set at subtest 86.
         *
         * That is why this is not the parameter search `RING.md` 73b warned
         * about: a fit changes behaviour to match a test, and here the two
         * candidates cannot be told apart by any observation this firmware
         * makes. The earlier acknowledge is taken, and the equivalence is
         * recorded so a later reader with a *second* firmware can settle it.
         *
         * Bit 3 is **set** rather than restored: the idle word `F806` has it
         * clear (finding 60) and subtest 16 requires it clear, so completion is
         * what turns it on. */
        ctl->a2.status |= (uint16_t)(AP_RING_CTL_STATUS_BIT2 |
                                     AP_RING_CTL_STATUS_BIT13 |
                                     AP_RING_CTL_STATUS_BIT3);
        ctl->a2.operation_pending = false;
        /* **The loopback's traffic, counted.** `002398-01` p. 6-32 gives the
         * diagnostic command bit `8000` as "dma test (loop xmit DMA to rcv
         * DMA)", so this family loops transmit into receive *internally* --
         * no medium, no station (`RING.md` 79a). The operation therefore moves
         * packets from one DMA to the other, and the receive counters count
         * what arrives.
         *
         * **How many is the firmware's own number, not one chosen here**: the
         * transmit packet counter `+C02` was loaded immediately before the
         * command (finding 72), and the transfer runs until it is exhausted.
         *
         * **Each counter is clocked from its own event, not from one shared
         * pulse**, because the part has three independent CLK pins and this
         * board drives them separately: `[EH]` p. 12-30 gives `RCV_STAT`'s bits
         * 2:0 as "pkt exceeded max_rcv_cnt", "data rcv in progress" and "hdr
         * rcv in progress", and p. 12-31's `XMIT_STAT` tags the transmit side
         * the same way -- `11` header being transmitted, `01` data being
         * transmitted, `00` message complete. Header and data are separate
         * phases on both sides, which is finding 81's separate DMA streams seen
         * on this board rather than on the DN5xx.
         *
         * That matters here even though all three still receive the same count:
         * `ap_i8254_clock` could not express the firmware's own requirement
         * that `RCV_HDR_CNT` and `RCV_PKT_CNT` reach *different* totals over
         * one transfer (1020 against 1023, finding 76a), because one pulse
         * cannot advance two counters by different amounts. The structure is
         * now able to hold the answer; what event makes the header arm fall
         * three short is still open (80c, 81b), and is deliberately not
         * guessed at here.
         *
         * `PROVISIONAL` in one further way: the transfer is *instantaneous*
         * here, where real hardware would spread it over the ring's bit clock.
         * Subtest 32 reads the counters afterwards and cannot tell the
         * difference; anything that watches them *during* a transfer could. */
        /* **All five counters are clocked by one source, and the firmware's
         * own expectation table says how many times.** `$AF8` walks `d4` 1..5
         * and gives each counter its expected reading; against `$976`'s
         * `$FFFF` preload and `$944`'s `$1FF`/`$3FF`, and remembering the 8254
         * counts *down*, every one of them works out to **1023** events:
         *
         *     XMIT_HDR  01FF -> FE00   1023
         *     XMIT_PKT  03FF -> 0000   1023
         *     RCV_PKT   FFFF -> FC00   1023
         *     RCV_MAX   FFFF -> FC00   1023
         *     RCV_HDR   FFFF -> FC03   1020
         *
         * So the loaded values are *preloads that make each counter
         * identifiable*, not limits, and the transmit pair are clocked too --
         * which this modelled not at all, so a run that got past `RCV_HDR`
         * would have failed at `d4 = 4` instead. */
        const uint16_t events = ctl->a2.timer_b.counter[1].latch;
        for (uint16_t i = 0; i < events; i++) {
          ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_PKT_CNT);
          ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_MAX_CNT);
          ap_i8254_clock_counter(&ctl->a2.timer_b, AP_RING_CTL_XMIT_HDR_CNT);
          ap_i8254_clock_counter(&ctl->a2.timer_b, AP_RING_CTL_XMIT_PKT_CNT);
          /* **`PROVISIONAL`: the receive header counter misses the frame start
           * sequence.** It alone counts three fewer, and `[MAC]` §2.2.2.1 puts
           * exactly three characters before the header sequence begins -- the
           * frame start character, a null separator and a separator character
           * (finding 87a, this project's own reading, which the station
           * already implements). A counter gated on "hdr rcv in progress"
           * (`RCV_STAT` bit 0) cannot be running during them.
           *
           * Modelled as the mechanism rather than as a bare `- 3`, because the
           * two are indistinguishable on this one number and only the first
           * predicts anything. **The cost of being wrong is named**: if the
           * three are not the frame start sequence, a transfer whose framing
           * differs would need a different offset, and nothing here would say
           * so. `RING.md` 120. */
          if (i >= AP_RING_FRAME_START_CHARACTERS) {
            ap_i8254_clock_counter(&ctl->a2.timer_a, AP_RING_CTL_RCV_HDR_CNT);
          }
        }
      } else if (!second_window) {
        /* Nothing outstanding: bit 2 still returns, since subtests 22 and 24
         * show the acknowledge is what brings it back, but the completion bits
         * do not -- there is no completion to report. */
        ctl->a2.status |= AP_RING_CTL_STATUS_BIT2;
      }
      return;
    case 4u:
      /* And `+4` acknowledges bit 1, from `$A28`'s tail. */
      if (!second_window) {
        ctl->a2.status |= AP_RING_CTL_STATUS_BIT1;
      }
      w->slot_004 = value;
      return;
    default:
      /* **The first window's `+006` is `TIMO_ACK`, not the RAM pointer.**
       * `002398-04` p. 12-29 puts `RAM_ADDR` at `59006` -- the *second*
       * window -- and `TIMO_ACK` at `51006`. This path set `w->pointer` for
       * both windows, so a write to the first window's `+006` armed a buffer
       * pointer that has no buffer behind it. Found by walking the page's
       * register table, not by a failure: the AT firmware never writes it, so
       * nothing could have caught it. */
      if (!second_window) {
        /* **An acknowledge clears the interrupt, and `tmi` is active low, so
         * acknowledging *sets* it.** This cleared it, which is the assertion
         * rather than the acknowledgement -- so a driver that acknowledged a
         * timeout was given one. `RING.md` 111 establishes the polarity from
         * `RING_PROC`'s `7A4D0944`: "a healthy board reads 1 and clear is the
         * pending timeout", and the `RCV_ACK` path twenty lines above already
         * states the rule for its twin -- "writing `RCV_ACK` at the first
         * window's `+4` sets it again".
         *
         * This is what Domain/OS died of. Its driver reads MISC_STAT, does
         * `lsr.b #1,d0` to put `tmi` in the carry and `bcs` past the error
         * call, so a clear bit *is* the error path -- and it read `F006`
         * (`FINDINGS.md` C208). */
        ctl->a2.status |= AP_RING_CTL_STATUS_TMI;
        return;
      }
      /* Finding 46: the pointer `+406` advances from. Writing it does **not**
       * prefetch -- if it did, the firmware's discarded first read would return
       * word 0 and every word after it would be one place early, which is the
       * off-by-one 46a exists to prevent. */
      /* Held as the buffer word it names, not as the word the driver wrote:
       * every consumer wants the address, and decoding once here is what keeps
       * `pointer_base` and the two descriptor registers on one convention. */
      w->pointer = ring_ctl_addr(value);
      w->pointer_base = w->pointer;
      return;
    }
  }

  /* The first window's second bank: four write-only registers, three of which
   * the AT firmware's `$944` writes in one run (`ap_ring_ctl.h`). They act on
   * the *second* window's status word, which is where MISC_STAT lives -- the
   * same split findings 74/74a established for the two acknowledges. */
  if (!second_window &&
      (offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u: /* ERR_BITS_CLR */
      ctl->a2.status &= (uint16_t)~AP_RING_CTL_STATUS_STICKY_ERRORS;
      return;
    case 2u: /* GPS_CLR -- the sticky good-packet-seen bit has its own clear */
      ctl->a2.status &= (uint16_t)~AP_RING_CTL_STATUS_GPS;
      return;
    case 4u: /* SOFT_XMIT_REQ: a request, not a clear. Nothing to assert -- the
              * page names it, no firmware here writes it, and inventing an
              * effect would be modelling the name rather than the part. */
      w->command_404 = value;
      return;
    default: /* LERR_CLR */
      w->slot_406 = value;
      return;
    }
  }

  if ((offset & AP_RING_CTL_BANK_MASK) == AP_RING_CTL_BANK_STATUS) {
    switch (offset & AP_RING_CTL_SLOT_MASK) {
    case 0u:
      /* **`+400` is status, and a write clears rather than stores.**
       *
       * Storing the host's bits was the reasonable default while nothing drove
       * this register. The firmware refutes it: subtest 01 requires
       * `(+400) & $F806 == $F806` at reset, and subtest 16 requires
       * `(+400) & $FF08 == $F000` *after* `move.b #$1,$400(a4)` and finding
       * 40's `$800`. A storing model answers `8100` to the second, because the
       * `01` it kept sits in bits 15-8 where the hardware keeps status.
       *
       * **Writing anything clears bit 11, and the manual said so first.**
       * Write-one-to-clear was tried and is refuted: it leaves bit 11 set after
       * `move.b #$1,$400(a4)`, and subtest 11 -- which passed before -- then
       * fails. The rule that fits all three data points is that a *write*, of
       * any value, clears it: set at reset for subtest 01, clear after the `#$1`
       * for subtest 11, and clear again under subtest 16's `$FF08` mask.
       *
       * And `[EH]`'s ring register section, finding 55, gives that behaviour in
       * words for the DN3xx board's transmit command: **"Writing anything to
       * this register clears the transmit interrupt."** A documented rule for
       * one generation of this controller, and the AT board's own self-test
       * requiring the same thing, is two independent sources rather than a fit
       * to three numbers.
       *
       * Bits 15-12 survive the write: they are status the board asserts, and
       * subtest 16 requires them set after it. The presence bit is one of them
       * and is held explicitly, which is why finding 40's `clr.w +400` does not
       * make the board vanish. */
      /* The connect state is taken from *this* write before the status is
       * recomputed from it: a driver that connects and then reads `nct` must
       * see the connection it just asked for, not the one before it. */
      w->connected = (value & AP_RING_CTL_MISC_CMD_NCT) != 0u;
      w->status = (uint16_t)(
          (w->status & (uint16_t) ~(AP_RING_CTL_STATUS_BIT11 |
                                    AP_RING_CTL_STATUS_PRESENT)) |
          ((ctl->present && !w->connected) ? AP_RING_CTL_STATUS_PRESENT : 0u));
      /* The write half is MISC_CMD, and `lpb` is kept: see the `+402` handler
       * for the three sites that make it the discriminator. */
      w->loopback_enabled = (value & AP_RING_CTL_MISC_CMD_LPB) != 0u;
      /* **MISC_CMD's `nct`, and it drives the bypass relay.** The read half is
       * MISC_STAT; the write half is MISC_CMD, whose bit 11 is "1 => network
       * connect" (p. 12-32). `RING.md` 103c confirms it here: `RING_PROC`
       * writes `$800` to connect and `$900` to connect with bit 8 `lpb`
       * digital loopback. §3.5's relay is what "connect" operates, and
       * `ap_ring_medium` has modelled both of its halves since finding 30 --
       * with nothing driving it until now.
       *
       * The AT firmware's own writes here are `$800` (finding 40) and the
       * `move.b #$1,$400` of subtest 11, so a board the *boot PROM* drives ends
       * up connected exactly as before this existed. */
      if (second_window && ctl->station != NULL && ctl->medium != NULL) {
        ap_ring_medium_set_bypass(ctl->medium, ctl->station->slot,
                                  (value & AP_RING_CTL_MISC_CMD_NCT) == 0u);
      }
      return;
    case 2u:
      w->command_402 = value;
      /* **The transmit command, and the frame leaves here for the medium.**
       *
       * `RING.md` 103d: both drivers write exactly `$0100`, `$0200` and
       * `$0600` to this register -- `RING_PROC` as words, the AT boot firmware
       * as the bytes `$1`, `$2`, `$6` in the high lane. The *values* are pinned
       * by two independent sources; their bit layout is not, so this dispatches
       * on the values rather than decoding bits it cannot justify.
       *
       * `$0200` transmits and `$0600` forces, which under p. 12-32's "force
       * transmit is a **modifier** to transmit enable" is the same operation
       * with one more bit. `$0100` is the third function and starts nothing.
       * A frame is queued for the first two, and for nothing else. */
      /* **`$0100`: the diagnostic's transmit, which loops back through the
       * gate array's own DMA.** `002398-01`'s `8000` is "dma test (loop xmit
       * DMA to rcv DMA)" (finding 79), so this moves a message from the
       * transmit side to the receive side inside the card, with no medium and
       * no station -- which is why it is separate from the `$0200`/`$0600`
       * path below rather than sharing it.
       *
       * What lands is `[MAC]` Figure 2-5's **received header**, not a copy of
       * what was written: `$B70` writes four words and `$BAC` reads six back
       * (`RING.md` 121). The gate array supplies the two the transmitter
       * inserts. */
      /* Detected by **mask, not equality**, and that distinction is why the
       * first version of this never fired: the firmware writes a *byte*
       * (`move.b #$1,$402`), which this model merges with the status lane, so
       * the word arriving here is `$01F0` and not `$0100`. Finding 66's `$6`
       * detection has masked on bit 10 for exactly the same reason. Bit 8 is
       * unique to `$1` -- `$2` is bit 9 and `$6` is bits 10 and 9. */
      /* `$1` arms a *packet* transmit and reads what it supplied; it does not
       * deposit. `$B70` issues it before `$976` sets `RCV_ADDR`, so a deposit
       * here would write over the message at its own address. */
      if (second_window && (value & 0x0100u) != 0u) {
        const uint16_t src = ring_ctl_addr(ctl->a2.slot_002);
        if ((size_t)src + 4u <= AP_RING_CTL_BUFFER_WORDS) {
          ctl->a2.xmit_intend_to_copy =
              (ctl->buffer[src + 3u] & AP_RING_EARLY_INTEND_TO_COPY) != 0u;
        }
        ctl->a2.xmit_packet = true;
      }
      /* **Transmit enable moves the data, whether or not it signals.**
       * `002398-01` p. 6-30: `2000 xmit enable (start the xmit)` and `4000
       * xmit interrupt enable` are separate bits, "to start a xmit normally,
       * use `6000`". The AT board's `$2` and `$6` carry the same low nibble,
       * so `$2` starts a transmit with the interrupt disabled -- the data
       * moves and the status bits do not change. Conflating the two left the
       * subtest 11-16 group's `#$2` never moving its pattern from `$600` to
       * `$610`, which is what subtest 42 reads. `RING.md` 131. */
      if (second_window && (value & 0x0200u) != 0u) {
        ring_ctl_block_move(ctl);
      }
      if (second_window && ctl->station != NULL &&
          (value == 0x0200u || value == 0x0600u)) {
        (void)ring_ctl_queue_from_buffer(ctl);
      }
      /* **`PROVISIONAL`: a `6` command completes an operation, and clears the
       * two status bits the firmware then waits on.**
       *
       * Subtest 22 writes `#$6` to `+402` and polls `+400`'s bits 13 and 2 with
       * `d4 = 0` -- finding 56b's polarity, so both must go *clear*. Subtest 16
       * requires bit 13 **set** after subtest 12 wrote `#$2` to the same
       * register, so it is the value that separates them, not the act of
       * writing. Finding 48 records `+402` taking `$1`, `$2` and `$6`.
       *
       * Modelled as: a command carrying `$4` completes immediately and clears
       * those two bits. That is the least this core can do and satisfy both
       * subtests, and it is an approximation in one specific way -- **real
       * hardware would set them busy and clear them when the operation
       * finished**, which is a timing this model does not have. The cost of
       * closing it is a transmit path with duration, which is the item this
       * belongs to. `[EH]`'s vocabulary for the DN3xx board calls bit 13 *busy*
       * and bit 2 *copy*, which fits a completion, but the AT board's command
       * encoding is plainly not the DN3xx's -- `$6` against that board's
       * `6000` -- so the name is not carried over. */
      /* **A `$2` completes only with digital loopback OFF, and that is the
       * only difference between the two sites that write it.** Subtest 12
       * (`0004A0`) and subtest 51 (`000628`) both write `#$2` after a
       * byte-identical `$976` / `#$8` to `+404` / `$1FF`/`$3FF` / `$944`
       * preamble, and both follow a `$B70` transmit -- yet 12 requires
       * `+400`'s bits 13 and 2 to **remain set** (`d4 = 1`) and 51 requires
       * them to go **clear**. The one thing that differs is `MISC_CMD`:
       * `00045C` writes `move.b #$1,$400` before 12's group and `000512`
       * writes `#$0` before 22's, which 51's group inherits. A byte at the
       * even address is the high lane, so `$1` is `$0100` -- p. 12-32's bit 8,
       * `lpb`, **digital loopback enable**.
       *
       * `$6` keeps completing unconditionally, which is what findings 66 and
       * 67 measured across all four of its sites. `RING.md` 123. */
      if ((value & 0x0400u) != 0u ||
          ((value & 0x0200u) != 0u && !w->loopback_enabled)) {
        w->status &= (uint16_t)~(AP_RING_CTL_STATUS_BIT13 |
                                 AP_RING_CTL_STATUS_BIT2 |
                                 AP_RING_CTL_STATUS_BIT1 |
                                 AP_RING_CTL_STATUS_BIT14);
        /* **The receive interrupt pends only if the frame asked to be
         * copied.** Subtest 24 follows a `$B70` carrying `$A` in `d4` --
         * Figure 2-7's intend-to-copy plus its parity bit -- and requires
         * `+400`'s bit 1 **clear**; subtest 63 follows one carrying `$0` and
         * requires it to **remain set** (`moveq #$1,d4` against 24's
         * `clr.l d4`). Same `#$6`, same `MISC_CMD` `$0`, same `$976`/`$944`
         * preamble: the transmitted early acknowledge is the only difference
         * between the two groups, and a frame nobody intended to copy raises
         * no receive interrupt. `RING.md` 126. */
        if (!w->xmit_intend_to_copy) {
          w->status |= AP_RING_CTL_STATUS_BIT1;
        }
        /* Something is now outstanding, and the acknowledge in the *first*
         * window is what finishes it (`RING.md` 74a, 75). */
        w->operation_pending = true;
        /* **The extent is bracketed, not chosen.** Two derived durations have
         * been tried and both refused: `RING.md` 70's 8 us (the 12-byte
         * minimum transmission) finished *before* subtest 22 polled, and the
         * firmware's own larger count at `[MAC]`'s 83.33 ns bit cell -- 1023
         * cells, 85 us -- had *not* finished by subtest 26. So the true extent
         * lies between, and picking a value inside that bracket, or picking
         * the smaller counter because the larger failed, is the parameter
         * search `CLAUDE.md` forbids. Completion stays immediate (finding 66's
         * `PROVISIONAL`) until the station drives it. `RING.md` 73. */
        /* Subtest 23: once the command has been taken the command lane reads
         * back **zero**, not the value written, and the status lane drops bit
         * 6 -- `B0` where an idle register reads `F0`. Both are the same event
         * seen in the two halves of one register, which is why they are done
         * together rather than as two rules. */
        w->command_402 = 0u;
        w->command_402_status &= (uint16_t)~0x0040u;
        /* **And `+404` with it: one completion, three registers.** Subtests 15
         * and 25 both follow `$976` (which writes zero to `+404`), a
         * `move.b #$8,$404(a4)`, and `$944` (which loads the 8254s) -- an
         * identical sequence. The only difference is the command written to
         * `+402` next: `#$2` before 15, which requires `+404`'s status bit 6
         * **set**, and `#$6` before 25, which requires it **clear** with the
         * command lane read back as zero. So the completing command is what
         * clears them, and this is the event that ends an operation rather than
         * three separate register rules. */
        w->command_404 = 0u;
        /* **And `ren` survives a frame nobody intended to copy.** Subtest 25
         * follows a `$B70` carrying `$A` and requires `(+404) & $FFF8 == $A0`
         * -- bit 6, `ren`, **clear**; subtest 64 follows one carrying `$0` and
         * requires `$E0`, with it **set**. Same `#$6`, same preamble: the
         * receive enable is consumed by a receive that happened, and a
         * transmission no receiver intended to copy leaves it armed. The same
         * discriminator as bit 1 above, one register along. `RING.md` 126a. */
        if (w->xmit_intend_to_copy) {
          w->command_404_status &= (uint16_t)~0x0040u;
        }
      }
      return;
    case 4u:
      w->command_404 = value;
      /* **`RCV_CMD`'s `rcv` drives `RCV_STAT`'s `ren`**, which is the two
       * halves of one register naming the same thing: p. 12-32 gives the write
       * half one bit, 11, "1 => receive enable", and p. 12-30 gives the read
       * half bit 6, `ren`, receive enable. Writing the enable arms the
       * receiver and the status says so; `$976`'s `move.w #$0,$404` disarms
       * it. Without this the bit was cleared by the first completing command
       * and never came back, so subtest 64 read `A0` where the firmware, which
       * had just written `#$8`, expected `E0`. */
      if (second_window) {
        if ((value & AP_RING_CTL_RCV_CMD_RCV) != 0u) {
          w->command_404_status |= 0x0040u;
        } else {
          w->command_404_status &= (uint16_t)~0x0040u;
        }
      }
      /* **RCV_CMD's `rcv`, and it drives the station.** `002398-04` p. 12-32
       * gives this register one bit, 11, "1 => receive enable", with "set
       * `rcv` to 0 to abort an enabled receive" -- and `RING.md` 103c confirms
       * it on *this* board from two independent drivers: `RING_PROC` writes
       * `$800` here and the AT boot firmware's `move.b #$8,$404` is the same
       * word. So this is the host telling the station whether to copy frames
       * addressed to it, which is exactly `receive_enabled`. */
      if (second_window && ctl->station != NULL) {
        ap_ring_station_set_receive_enabled(
            ctl->station, (value & AP_RING_CTL_RCV_CMD_RCV) != 0u);
      }
      return;
    default:
      if (!second_window) {
        w->slot_406 = value;
        return;
      }
      /* The write side of the port, advancing the same pointer the read side
       * does -- which is what lets the firmware fill from `+006 = 0` and then
       * read back from `+006 = 0`. */
      if (w->pointer < AP_RING_CTL_BUFFER_WORDS) {
        ctl->buffer[w->pointer] = value;
      }
      w->pointer++;
      return;
    }
  }

  ap_ring_ctl_write8(ctl, second_window, offset, (uint8_t)(value >> 8));
  ap_ring_ctl_write8(ctl, second_window, offset | 1u, (uint8_t)(value & 0xFFu));
}

void ap_ring_ctl_clock(ap_ring_ctl_t *ctl, bool second_window) {
  if (ctl == NULL) {
    return;
  }
  ap_ring_ctl_window_t *w = window_of(ctl, second_window);
  if (second_window) {
    /* The board's two 8254s are both in this window -- p. 12-29's `59800` and
     * `59C00` banks. The first window is PROM and has none. */
    ap_i8254_clock(&w->timer_a);
    ap_i8254_clock(&w->timer_b);
    ap_ring_ctl_poll_ring(ctl);
  }
}
