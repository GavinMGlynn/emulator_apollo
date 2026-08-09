/* Whole frames over the ring's bit stream. See ap_ring_framer.h for the
 * sequence order and its citations. */

#include "ring/ap_ring_framer.h"

/* The short null separator, "a byte of Zeros", written as data so the stuffing
 * rule applies to it exactly as to any other byte. */
#define NULL_SEPARATOR_BITS 8u

static ap_ring_frame_parse_t failed(ap_ring_frame_status_t status) {
  return (ap_ring_frame_parse_t){.status = status};
}

bool ap_ring_frame_emit(ap_ring_bitwriter_t *w,
                        const ap_ring_frame_fields_t *fields) {
  /* Checked first, all of it: a writer that discovered the length was wrong
   * halfway would leave a partial frame in the stream, and the next thing to
   * read it would find a frame start with no end. */
  if (!ap_ring_header_length_valid(fields->header_bytes) ||
      !ap_ring_data_length_valid(fields->data_bytes)) {
    return false;
  }

  /* 1. Frame start sequence: the character, a null separator, a separator. */
  ap_ring_write_oob(w, AP_RING_OOB_FRAME_START);
  ap_ring_write_data_bits(w, 0u, NULL_SEPARATOR_BITS);
  ap_ring_write_oob(w, AP_RING_OOB_SEPARATOR);

  /* 2. Packet header sequence. */
  for (size_t i = 0; i < fields->header_bytes; i++) {
    ap_ring_write_data_bits(w, fields->header[i], 8u);
  }
  ap_ring_write_oob(w, AP_RING_OOB_SEPARATOR);

  /* 3. Packet data sequence. Zero bytes of data is legal and still ends with
   * its separator: the sequence is what the manual names, not the field. */
  for (size_t i = 0; i < fields->data_bytes; i++) {
    ap_ring_write_data_bits(w, fields->data[i], 8u);
  }
  ap_ring_write_oob(w, AP_RING_OOB_SEPARATOR);

  /* 4. Frame check sequence: the CRC most-significant bit first, then a null
   * separator. The CRC is over the header and data sequences and the
   * separators, with the early acknowledge byte counted as zeros -- all of
   * which `ap_ring_frame_crc` already knows. */
  const uint32_t crc = ap_ring_frame_crc(fields->header, fields->header_bytes,
                                         fields->data, fields->data_bytes);
  ap_ring_write_data_bits(w, crc, 32u);
  ap_ring_write_data_bits(w, 0u, NULL_SEPARATOR_BITS);

  /* 5. End-of-frame sequence. */
  ap_ring_write_data_bits(w, fields->late_acknowledge, 8u);

  /* Reported at the end because the writer's overflow is sticky: one check
   * covers every write above, and none of them can have written past the
   * buffer to get here. */
  return !w->overflow;
}

/* One data byte, with the violation folded into the failure: a byte that ran
 * into an out-of-band character is a malformed sequence, since a caller only
 * asks for a byte once a peek has said no character stands next. */
static bool read_byte(ap_ring_bitreader_t *r, uint8_t *out) {
  uint32_t value = 0u;
  bool violation = false;
  if (!ap_ring_read_data_bits(r, 8u, &value, &violation) || violation) {
    return false;
  }
  *out = (uint8_t)value;
  return true;
}

/* A separator *character*, which is what ends the header and data sequences. */
static bool read_separator(ap_ring_bitreader_t *r) {
  uint16_t symbol = 0u;
  return ap_ring_read_oob(r, &symbol) &&
         symbol == (uint16_t)AP_RING_OOB_SEPARATOR;
}

ap_ring_frame_parse_t ap_ring_frame_parse(ap_ring_bitreader_t *r,
                                          uint8_t *header,
                                          size_t header_capacity, uint8_t *data,
                                          size_t data_capacity) {
  ap_ring_frame_parse_t out = {0};

  /* 1. Frame start sequence. */
  uint16_t symbol = 0u;
  if (!ap_ring_read_oob(r, &symbol)) {
    return failed(AP_RING_FRAME_TRUNCATED);
  }
  if (symbol != (uint16_t)AP_RING_OOB_FRAME_START) {
    return failed(AP_RING_FRAME_NO_FRAME_START);
  }
  uint8_t null_separator = 0u;
  if (!read_byte(r, &null_separator)) {
    return failed(AP_RING_FRAME_TRUNCATED);
  }
  if (null_separator != 0u) {
    return failed(AP_RING_FRAME_BAD_NULL);
  }
  if (!read_separator(r)) {
    return failed(AP_RING_FRAME_BAD_SEPARATOR);
  }

  /* 2. Packet header sequence, taken byte by byte until a character arrives --
   * the length is not on the wire, so the separator is what ends it. */
  while (!ap_ring_peek_oob(r, &symbol)) {
    if (out.header_bytes >= header_capacity) {
      return failed(AP_RING_FRAME_HEADER_LENGTH);
    }
    if (!read_byte(r, &header[out.header_bytes])) {
      return failed(AP_RING_FRAME_TRUNCATED);
    }
    out.header_bytes++;
  }
  if (!ap_ring_header_length_valid(out.header_bytes)) {
    return failed(AP_RING_FRAME_HEADER_LENGTH);
  }
  if (!read_separator(r)) {
    return failed(AP_RING_FRAME_BAD_SEPARATOR);
  }

  /* 3. Packet data sequence, the same way. */
  while (!ap_ring_peek_oob(r, &symbol)) {
    if (out.data_bytes >= data_capacity) {
      return failed(AP_RING_FRAME_DATA_LENGTH);
    }
    if (!read_byte(r, &data[out.data_bytes])) {
      return failed(AP_RING_FRAME_TRUNCATED);
    }
    out.data_bytes++;
  }
  if (!ap_ring_data_length_valid(out.data_bytes)) {
    return failed(AP_RING_FRAME_DATA_LENGTH);
  }
  if (!read_separator(r)) {
    return failed(AP_RING_FRAME_BAD_SEPARATOR);
  }

  /* 4. Frame check sequence. Fixed at 32 bits, so this one is counted rather
   * than delimited, and the null separator follows it. */
  bool violation = false;
  if (!ap_ring_read_data_bits(r, 32u, &out.check_received, &violation) ||
      violation) {
    return failed(AP_RING_FRAME_TRUNCATED);
  }
  if (!read_byte(r, &null_separator)) {
    return failed(AP_RING_FRAME_TRUNCATED);
  }
  if (null_separator != 0u) {
    return failed(AP_RING_FRAME_BAD_NULL);
  }

  /* 5. End-of-frame sequence. */
  if (!read_byte(r, &out.late_acknowledge)) {
    return failed(AP_RING_FRAME_TRUNCATED);
  }

  out.check_computed =
      ap_ring_frame_crc(header, out.header_bytes, data, out.data_bytes);
  out.status = (out.check_computed == out.check_received)
                   ? AP_RING_FRAME_OK
                   : AP_RING_FRAME_CHECK_FAILED;
  return out;
}
