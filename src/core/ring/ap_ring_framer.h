/* Apollo token ring: whole frames over the bit stream.
 *
 * `[MAC]` 010005-00 §2.2.2 pp. 2-6 ff., read as page images.
 *
 * `ap_ring_mac` gives the symbols and the stuffing, `ap_ring_frame` gives the
 * field layouts and the check polynomial. Neither says what order they go in,
 * and that is this module: "MAC protocols dictate that each sequence in a frame
 * must appear in the appropriate order, and that the data within each sequence
 * must be formatted correctly, or protocol errors will occur."
 *
 * The five sequences, and exactly what each contains:
 *
 *   1. **Frame start** (§2.2.2.1) -- "the frame start (out-of-band) character,
 *      a null separator, and a separator character". The null separator here is
 *      the *short* one, a byte of zeros (§2.2.1.3).
 *   2. **Packet header** (§2.2.2.2) -- "a packet header and a separator
 *      character".
 *   3. **Packet data** (§2.2.2.3) -- "packet data and a separator character".
 *   4. **Frame check** (§2.2.2.4) -- "a cyclic redundancy field and a null
 *      separator", the CRC sent "most-significant bit first".
 *   5. **End-of-frame** (§2.2.2.5) -- "the late acknowledge field".
 *
 * ## Why a receiver needs `ap_ring_peek_oob`
 *
 * Neither variable-length sequence carries its length: a packet header "can
 * vary in size from 12 to 1024 bytes" and packet data "from 0 to 4096 bytes",
 * and nothing on the wire announces which. A receiver takes bytes until the
 * separator character arrives, and therefore has to ask whether one stands next
 * *before* consuming anything. That is the whole reason the peek exists.
 *
 * ## What this module does not do
 *
 * The long null separator is not part of a frame. §2.1 puts it in the *transmit*
 * sequence -- a node "pads with Zeros just after the modified token" -- before
 * the frame proper begins, so it belongs to the station and not here.
 *
 * Nor is aborting modelled: "the controller may abort transmitting the packet
 * header field on any even byte after the 12th", and a real controller does that
 * from an error it has detected mid-transmission. There is no error source here
 * to raise one, so a frame is emitted whole. Recorded rather than silently
 * omitted, since the *effect* of an abort -- the error bit in the late
 * acknowledge field -- is expressible and its cause is not.
 */

#ifndef APOLLO_RING_AP_RING_FRAMER_H
#define APOLLO_RING_AP_RING_FRAMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring/ap_ring_frame.h"
#include "ring/ap_ring_mac.h"

/* What a transmitter is given: the two variable-length fields and the late
 * acknowledge byte it inserts. The CRC is computed here rather than supplied,
 * because it is a function of the other two and a caller that could pass a
 * wrong one would be able to emit a frame no node accepts. */
typedef struct {
  const uint8_t *header;
  size_t header_bytes;
  const uint8_t *data;
  size_t data_bytes;
  uint8_t late_acknowledge;
} ap_ring_frame_fields_t;

/* Emit a whole frame. False when the lengths are not what §2.2.2 permits, or
 * when the writer ran out of room -- checked before anything is written, so a
 * refused frame leaves the stream untouched rather than half-formed. */
[[nodiscard]] bool ap_ring_frame_emit(ap_ring_bitwriter_t *w,
                                      const ap_ring_frame_fields_t *fields);

/* How a parse ended. A frame that arrives intact but fails its check is
 * distinct from one that was malformed: the first is a frame this node saw and
 * must report an error for, the second never was a frame. */
typedef enum {
  AP_RING_FRAME_OK,
  AP_RING_FRAME_TRUNCATED,       /* the stream ended mid-frame */
  AP_RING_FRAME_NO_FRAME_START,  /* the first character was not frame start */
  AP_RING_FRAME_BAD_SEPARATOR,   /* a sequence did not end with a separator */
  AP_RING_FRAME_BAD_NULL,        /* a null separator held something other than
                                  * zeros */
  AP_RING_FRAME_HEADER_LENGTH,   /* not 12-1024, or odd, or past the buffer */
  AP_RING_FRAME_DATA_LENGTH,     /* not 0-4096, or odd, or past the buffer */
  AP_RING_FRAME_CHECK_FAILED,    /* whole and well-formed, wrong CRC */
} ap_ring_frame_status_t;

typedef struct {
  ap_ring_frame_status_t status;
  size_t header_bytes;
  size_t data_bytes;
  uint8_t late_acknowledge;
  /* Both, always, so a failed check can be read rather than merely counted.
   * They are equal exactly when `status` is `AP_RING_FRAME_OK`. */
  uint32_t check_received;
  uint32_t check_computed;
} ap_ring_frame_parse_t;

/* Parse one frame, filling the caller's header and data buffers.
 *
 * The CRC is recomputed over what arrived and compared with what the frame
 * carried, which is the receiver's own job: "the receiver calculates the CRC to
 * include the packet header and data sequences, and the separators (treating
 * the early acknowledge field as 'Zeros', and ignoring those bits added during
 * the bit-stuffing process)". */
[[nodiscard]] ap_ring_frame_parse_t
ap_ring_frame_parse(ap_ring_bitreader_t *r, uint8_t *header,
                    size_t header_capacity, uint8_t *data,
                    size_t data_capacity);

#endif /* APOLLO_RING_AP_RING_FRAMER_H */
