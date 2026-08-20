/* Apollo Token Ring — MAC frame level, `[MAC]` §2.2.2 (pp. 2-6 to 2-9).
 *
 * The symbol level is `ap_ring_mac.h`; this is what those symbols delimit. As
 * there, every figure was read from a `pdftoppm` rendering rather than from the
 * text layer, which does not survive these diagrams.
 *
 * ## A frame is five sequences
 *
 * `[MAC]` §2.2.2 p. 2-6 names them, and each is a payload followed by a
 * delimiter:
 *
 * ```
 *   1. frame start    frame-start character, null separator, separator
 *   2. packet header  packet header, separator
 *   3. packet data    packet data, separator
 *   4. frame check    32-bit CRC, null separator
 *   5. end-of-frame   late acknowledge field
 * ```
 *
 * ## The two acknowledge fields are written by *other* nodes
 *
 * This is the part with no equivalent in a point-to-point link, and it is why
 * the CRC has the exception it does. The transmitter inserts an early
 * acknowledge field and "another node's receiver modifies it" (§2.2.2.2 p.
 * 2-8) as the frame passes; likewise the late acknowledge field in the
 * end-of-frame sequence. A receiver rewriting a byte in flight would invalidate
 * any ordinary frame check, so "ring hardware treats this field as a string of
 * Zeros in its CRC calculation" -- the early acknowledge byte contributes zeros
 * whatever it actually holds, and no CRC needs recomputing.
 *
 * The late acknowledge field is outside the CRC entirely: §2.2.2.4 says the
 * receiver's CRC covers "the packet header and data sequences, and the
 * separators", and the end-of-frame sequence is neither.
 */

#ifndef APOLLO_RING_AP_RING_FRAME_H
#define APOLLO_RING_AP_RING_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ## Packet header, `[MAC]` Figure 2-5 p. 2-6
 *
 * ```
 *   +0   Destination Address (2 words)
 *   +4   Type (1 word)
 *   +6   Zeros (1-byte separator) | Early Acknowledge (1 byte)
 *   +8   Source Address (2 words)
 *   +C   Header Data (0 - 1012 bytes)
 * ```
 *
 * "Although a packet header can vary in size from 12 to 1024 bytes, it must
 * always consist of an even number of bytes." The fixed part is those twelve
 * bytes and 12 + 1012 = 1024, so the two figures agree. */
#define AP_RING_HDR_DESTINATION 0u
#define AP_RING_HDR_TYPE 4u
#define AP_RING_HDR_ZEROS 6u
#define AP_RING_HDR_EARLY_ACK 7u
#define AP_RING_HDR_SOURCE 8u
#define AP_RING_HDR_DATA 12u

#define AP_RING_HDR_FIXED_BYTES 12u
#define AP_RING_HDR_MAX_BYTES 1024u
#define AP_RING_HDR_DATA_MAX_BYTES 1012u

/* "The controller will always transmit the first 12 bytes of a packet header
 * ... even in a broken network", which is what beaconing depends on. Kept as a
 * named constant because it is a *guarantee* and not merely the size of the
 * fixed fields, even though the two numbers coincide. */
#define AP_RING_HDR_GUARANTEED_BYTES 12u

/* Packet data: "can vary in size from 0 to 4096 bytes, but it must always
 * consist of an even number of bytes. Typically, packet data consists of 1024
 * bytes." §2.2.2.3 p. 2-8. */
#define AP_RING_DATA_MAX_BYTES 4096u
#define AP_RING_DATA_TYPICAL_BYTES 1024u

/* ## Type field, `[MAC]` Figure 2-6 p. 2-7
 *
 * A 16-bit word: bits 15:8 reserved, bits 7:1 the type field proper, bit 0
 * reserved. So the named bits are 7 down to 1 and *bit 0 is not a type bit*. */
#define AP_RING_TYPE_BROADCAST (1u << 7)      /* receivers ignore destination */
#define AP_RING_TYPE_HW_DIAGNOSTICS (1u << 6) /* diagnostics only */
#define AP_RING_TYPE_THANK_YOU (1u << 5)      /* the packet is a reply */
#define AP_RING_TYPE_PLEASE (1u << 4)         /* the packet is a request */
#define AP_RING_TYPE_PAGING (1u << 3)         /* page-outs, protocol changes */
#define AP_RING_TYPE_USER (1u << 2)           /* interprocess, not the OS */
#define AP_RING_TYPE_SW_DIAGNOSTICS (1u << 1) /* monitoring and maintenance */
#define AP_RING_TYPE_RESERVED_MASK 0xFF01u    /* 15:8 and 0 */

/* ## Early acknowledge, `[MAC]` Figure 2-7 p. 2-8
 *
 * ```
 *   7      must be zero
 *   6:5    reserved
 *   4      must be zero
 *   3      intend-to-copy   an addressed receiver sets this
 *   2      reserved
 *   1      parity           odd
 *   0      must be zero
 * ``` */
#define AP_RING_EARLY_INTEND_TO_COPY (1u << 3)
#define AP_RING_EARLY_PARITY (1u << 1)
#define AP_RING_EARLY_MUST_BE_ZERO 0x91u /* bits 7, 4 and 0 */

/* ## Late acknowledge, `[MAC]` Figure 2-8 p. 2-9
 *
 * ```
 *   7      must be zero
 *   6      copied           receiver copied the packet without errors
 *   5      wait ack         addressed receiver was not enabled to copy
 *   4      must be zero
 *   3      intend-to-copy   addressed receiver set up to copy, type matched
 *   2      error            an error was observed, or the sender aborted
 *   1      parity           odd
 *   0      must be zero
 * ``` */
#define AP_RING_LATE_COPIED (1u << 6)
#define AP_RING_LATE_WAIT_ACK (1u << 5)
#define AP_RING_LATE_INTEND_TO_COPY (1u << 3)
#define AP_RING_LATE_ERROR (1u << 2)
#define AP_RING_LATE_PARITY (1u << 1)
#define AP_RING_LATE_MUST_BE_ZERO 0x91u /* bits 7, 4 and 0 */

/* ## A fifth source on these four bits, and the question it leaves
 *
 * The ring has no runnable oracle, so every independent description of it is
 * worth recording. `002398-04` p. 7-29 gives the **DN3xx ring controller's
 * transmit status register**, and four of its sixteen bits are these:
 *
 *     0010  icopy            (somebody Intended to COPY -- was willing to rcv)
 *     0008  ack byte errbit  (somebody (anybody!) set the "error detected" bit)
 *     0004  copy             (somebody did COPY the pkt)
 *     0002  wack
 *
 * with two worked values: "a successful transmit will have a transmit status of
 * **0014**" -- `icopy | copy` -- "and a **WACK** will have a transmit status of
 * **0012**" -- `icopy | wack`.
 *
 * The success case agrees with this model exactly: `ap_ring_station` sets
 * `COPIED` and `INTEND_TO_COPY` together on the copy path, and
 * `ring_station_suite` asserts both.
 *
 * **The WACK case is the open question.** This model sets the late field's
 * intend-to-copy only when the addressed receiver is `receive_enabled`, so a
 * wait-ack carries `WAIT_ACK` without it -- and p. 7-29 has `icopy` set beside
 * `wack`. The two are only in conflict if that register's `icopy` mirrors the
 * **late** field. It may well mirror the **early** one: `AP_RING_EARLY_INTEND_TO_COPY`
 * is set by the addressee before it knows whether the copy will succeed, which
 * is true in both cases and makes `0012` and `0014` differ in exactly the bit
 * that distinguishes them.
 *
 * `[MAC]` Figure 2-8 supports this model as it stands -- it glosses the late
 * field's bit as "addressed receiver **set up to copy**, type matched", which a
 * receiver that is not enabled is not. So nothing is changed. What is recorded
 * is that a second document gives two exact status words for the two outcomes,
 * and that reproducing them would settle which acknowledge field the DN3xx
 * controller's `icopy` reflects. That is a different controller generation from
 * the one this core models, which is why it is a question and not a defect. */

/* Both acknowledge fields carry **odd** parity in bit 1: "When it is set, an
 * odd number of Ones appears in the frame's ... acknowledge field." Read
 * carefully, that describes the field *including* the parity bit, so the rule
 * is that a well-formed field always has an odd population count. Returns the
 * field with bit 1 set or cleared to make that true. */
[[nodiscard]] uint8_t ap_ring_ack_with_parity(uint8_t field);

/* Whether a field's population count is odd, which is the check a receiver
 * makes. */
[[nodiscard]] bool ap_ring_ack_parity_ok(uint8_t field);

/* ## The frame check sequence, `[MAC]` §2.2.2.4 p. 2-8
 *
 * "The 32-bit cyclic redundancy check is initialized to zero and based on the
 * following generator polynomial: g(X) = (X^21 + 1)(X^11 + X^2 + 1)."
 *
 * Expanded, that is **X^32 + X^23 + X^21 + X^11 + X^2 + 1** -- degree 32, as it
 * must be. Dropping the X^32 term leaves `0x00A00805`.
 *
 * **This is not the Ethernet CRC-32** (`0x04C11DB7`), and the difference is the
 * whole reason this is written out rather than reached for from a library. A
 * ring implementation that quietly used the familiar polynomial would produce
 * frames that no Apollo node accepts, and nothing in a round-trip test against
 * itself would notice.
 *
 * "The sender transmits the CRC most-significant bit first", so the register is
 * shifted left and fed MSB-first, with no reflection and no final inversion --
 * the manual gives an initial value of zero and says nothing about either. */
#define AP_RING_CRC_POLYNOMIAL 0x00A00805u
#define AP_RING_CRC_INIT 0u

/* Feed `bits` of `value`, most-significant first. */
[[nodiscard]] uint32_t ap_ring_crc_bits(uint32_t crc, uint32_t value,
                                        unsigned bits);

/* Feed whole bytes, most-significant bit of each byte first. */
[[nodiscard]] uint32_t ap_ring_crc_bytes(uint32_t crc, const uint8_t *bytes,
                                         size_t count);

/* The CRC over a frame's covered material: the packet header and data
 * sequences and their separators, with the early acknowledge byte contributed
 * as zero whatever it holds, and stuffing bits excluded.
 *
 * The separators are the *characters* between the sequences; they are fed as
 * their nine-bit symbols, since that is what is on the wire and the manual says
 * the CRC covers "the separators" without qualifying them as anything else.
 * That reading is marked PROVISIONAL in `RING.md`: §2.2.2.4 does not say
 * whether a separator contributes its nine bits or is counted some other way,
 * and no capture exists to settle it. */
[[nodiscard]] uint32_t ap_ring_frame_crc(const uint8_t *header,
                                         size_t header_bytes,
                                         const uint8_t *data,
                                         size_t data_bytes);

/* ## Validity, as the manual states it rather than as convenience suggests */

/* "must always consist of an even number of bytes", 12 to 1024 inclusive. */
[[nodiscard]] bool ap_ring_header_length_valid(size_t bytes);

/* 0 to 4096 inclusive, even. */
[[nodiscard]] bool ap_ring_data_length_valid(size_t bytes);

/* Field accessors over a packet header buffer. Big-endian on the wire: the
 * ring is a Motorola machine's network and every multi-byte figure in `[MAC]`
 * is drawn most-significant first. */
[[nodiscard]] uint32_t ap_ring_header_destination(const uint8_t *header);
[[nodiscard]] uint32_t ap_ring_header_source(const uint8_t *header);
[[nodiscard]] uint16_t ap_ring_header_type(const uint8_t *header);
[[nodiscard]] uint8_t ap_ring_header_early_ack(const uint8_t *header);

void ap_ring_header_set_destination(uint8_t *header, uint32_t address);
void ap_ring_header_set_source(uint8_t *header, uint32_t address);
void ap_ring_header_set_type(uint8_t *header, uint16_t type);
void ap_ring_header_set_early_ack(uint8_t *header, uint8_t field);

/* Whether a receiver at `node` accepts this header: "a node receives a message
 * if the destination address field matches its node address, or if the
 * broadcast bit in the type field ... is set" (§2.2.2.2 p. 2-7). Apollo ring
 * protocols "do not support 'grouped' addresses", so there is no third case. */
[[nodiscard]] bool ap_ring_header_addresses(const uint8_t *header,
                                            uint32_t node);

#endif /* APOLLO_RING_AP_RING_FRAME_H */
