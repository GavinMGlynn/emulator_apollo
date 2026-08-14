#include "device/ap_3c505.h"

#include <stddef.h>

bool ap_3c505_command_is_implemented(uint8_t code) {
  return code >= AP_3C505_CMD_FIRST && code <= AP_3C505_CMD_LAST;
}

bool ap_3c505_response_for(uint8_t command, uint8_t *response) {
  if (!ap_3c505_command_is_implemented(command)) {
    return false;
  }
  /* `[DEV]` Table 1 marks `36` and `37` `n/a`: the PIO transfers are the two
   * implemented commands the adapter never answers, because the host moves the
   * data itself and has nothing to be asked for. */
  if (command == AP_3C505_CMD_DOWNLOAD_DATA_PIO ||
      command == AP_3C505_CMD_UPLOAD_DATA_PIO) {
    return false;
  }
  if (response != NULL) {
    *response = (uint8_t)(command + AP_3C505_RESPONSE_BIAS);
  }
  return true;
}

bool ap_3c505_decode(uint32_t base, uint32_t address, uint32_t *offset) {
  if (address < base || address >= base + AP_3C505_IO_SIZE) {
    return false;
  }
  if (offset != NULL) {
    *offset = address - base;
  }
  return true;
}

void ap_3c505_reset(ap_3c505_t *card) {
  if (card == NULL) {
    return;
  }
  const bool test_jumper = card->test_jumper;
  const bool sixteen_bit = card->sixteen_bit;
  *card = (ap_3c505_t){0};
  /* Strapping survives a reset because it is a jumper and a slot, not state.
   * Clearing them here would make `ASR` change under a hard reset, which is a
   * thing the host can do and the jumper cannot notice. */
  card->test_jumper = test_jumper;
  card->sixteen_bit = sixteen_bit;
}

/* The FIFO's capacity in the direction `DIR` currently names. Half duplex, so
 * "ready" means room to put a byte one way and a byte to take the other. */
static bool host_to_adapter(const ap_3c505_t *card) {
  return (card->hcr & AP_3C505_HCR_DIR) == 0u;
}

uint8_t ap_3c505_host_status(const ap_3c505_t *card) {
  uint8_t status = 0;
  /* The adapter's general-purpose flags, passed through and interpreted
   * nowhere: `[DEV]` §1.9.5 says the hardware "does not decode them in any
   * way", so a model that gave them meaning would be inventing one. */
  status |= (uint8_t)(card->acr & (AP_3C505_ACR_ASF1 | AP_3C505_ACR_ASF2 |
                                   AP_3C505_ACR_ASF3));
  if (card->dma_done) {
    status |= AP_3C505_HSR_DONE;
  }
  if ((card->hcr & AP_3C505_HCR_DIR) != 0u) {
    status |= AP_3C505_HSR_DIR;
  }
  /* One byte's occupancy, seen from the host: a byte waiting *for* it, and its
   * own outgoing byte having been taken. */
  if (card->to_host_full) {
    status |= AP_3C505_HSR_ACRF;
  }
  if (!card->to_adapter_full) {
    status |= AP_3C505_HSR_HCRE;
  }
  if (host_to_adapter(card) ? (card->fifo_count < AP_3C505_DATA_FIFO)
                            : (card->fifo_count > 0u)) {
    status |= AP_3C505_HSR_HRDY;
  }
  return status;
}

uint8_t ap_3c505_adapter_status(const ap_3c505_t *card) {
  uint8_t status = 0;
  status |= (uint8_t)(card->hcr & (AP_3C505_HCR_HSF1 | AP_3C505_HCR_HSF2));
  if (card->test_jumper) {
    status |= AP_3C505_ASR_SWTC;
  }
  if (card->sixteen_bit) {
    status |= AP_3C505_ASR_8_16;
  }
  if ((card->hcr & AP_3C505_HCR_DIR) != 0u) {
    status |= AP_3C505_ASR_DIR;
  }
  /* The same byte, from the other side, which is why these are derived: `HCRF`
   * here and `HCRE` in `HSR` are one fact and cannot disagree. */
  if (card->to_adapter_full) {
    status |= AP_3C505_ASR_HCRF;
  }
  if (!card->to_host_full) {
    status |= AP_3C505_ASR_ACRE;
  }
  /* And the FIFO from the adapter's end: the direction that is "ready" for the
   * host is the one that is *not* ready for the adapter. */
  if (host_to_adapter(card) ? (card->fifo_count > 0u)
                            : (card->fifo_count < AP_3C505_DATA_FIFO)) {
    status |= AP_3C505_ASR_ARDY;
  }
  return status;
}

static void flush_fifo(ap_3c505_t *card) { card->fifo_count = 0; }

uint8_t ap_3c505_read(ap_3c505_t *card, unsigned offset) {
  if (card == NULL) {
    return 0xFFu;
  }
  switch (offset) {
  case AP_3C505_REG_COMMAND:
    /* Reading takes the byte, which is what clears `ACRF`. A read of an empty
     * register returns the stale byte rather than inventing one, and leaves the
     * flag clear -- the host is not supposed to read without checking. */
    card->to_host_full = false;
    return card->to_host;
  case AP_3C505_REG_STATUS:
    return ap_3c505_host_status(card);
  case AP_3C505_REG_DATA: {
    if (host_to_adapter(card) || card->fifo_count == 0u) {
      /* Reading against the direction, or from an empty FIFO. The FIFO is half
       * duplex and there is nothing to hand back. */
      return 0xFFu;
    }
    const uint8_t value = card->fifo[0];
    for (unsigned i = 1; i < card->fifo_count; i++) {
      card->fifo[i - 1u] = card->fifo[i];
    }
    card->fifo_count--;
    return value;
  }
  case AP_3C505_REG_CONTROL:
    /* "and read on Rev 3 hardware" -- the register reads back what was written
     * rather than answering `FF` like the write-only locations. */
    return card->hcr;
  default:
    break;
  }
  return 0xFFu;
}

void ap_3c505_write(ap_3c505_t *card, unsigned offset, uint8_t value) {
  if (card == NULL) {
    return;
  }
  switch (offset) {
  case AP_3C505_REG_COMMAND:
    card->to_adapter = value;
    card->to_adapter_full = true;
    return;
  case AP_3C505_REG_AUX_DMA:
    card->aux_dma = value;
    return;
  case AP_3C505_REG_DATA:
    if (!host_to_adapter(card) || card->fifo_count >= AP_3C505_DATA_FIFO) {
      /* Against the direction, or full: the byte is lost, which is what a FIFO
       * with no room does and is why `HRDY` exists to be polled. */
      return;
    }
    card->fifo[card->fifo_count++] = value;
    return;
  case AP_3C505_REG_CONTROL: {
    const uint8_t previous = card->hcr;
    card->hcr = value;
    /* `ATTN` and `FLSH` together are the host's hard reset, so it is checked
     * before the plain flush -- the pair is not "a flush that also interrupts".
     */
    if ((value & AP_3C505_HCR_HARD_RESET) == AP_3C505_HCR_HARD_RESET) {
      ap_3c505_reset(card);
      return;
    }
    if ((value & AP_3C505_HCR_FLSH) != 0u) {
      flush_fifo(card);
    }
    /* A change of direction empties the buffer: it is one FIFO, and bytes put
     * in for one direction are not readable in the other. */
    if (((previous ^ value) & AP_3C505_HCR_DIR) != 0u) {
      flush_fifo(card);
    }
    return;
  }
  default:
    return;
  }
}

bool ap_3c505_adapter_take_command(ap_3c505_t *card, uint8_t *command) {
  if (card == NULL || !card->to_adapter_full) {
    return false;
  }
  if (command != NULL) {
    *command = card->to_adapter;
  }
  card->to_adapter_full = false;
  return true;
}

void ap_3c505_adapter_post_command(ap_3c505_t *card, uint8_t response) {
  if (card == NULL) {
    return;
  }
  card->to_host = response;
  card->to_host_full = true;
}

void ap_3c505_adapter_write_control(ap_3c505_t *card, uint8_t value) {
  if (card == NULL) {
    return;
  }
  card->acr = value;
  if ((value & AP_3C505_ACR_FLSH) != 0u) {
    flush_fifo(card);
  }
}

bool ap_3c505_irq(const ap_3c505_t *card) {
  if (card == NULL) {
    return false;
  }
  /* §1.10, and the two enables are independent: a command byte waiting with
   * `CMDE` set, or terminal count reached with `TCEN` set. */
  if (card->to_host_full && (card->hcr & AP_3C505_HCR_CMDE) != 0u) {
    return true;
  }
  return card->dma_done && (card->hcr & AP_3C505_HCR_TCEN) != 0u;
}

ap_3c505_sf_t ap_3c505_host_flags(const ap_3c505_t *card) {
  if (card == NULL) {
    return AP_3C505_SF_UNDEFINED;
  }
  const unsigned low = (card->hcr & AP_3C505_HCR_HSF1) != 0u ? 1u : 0u;
  const unsigned high = (card->hcr & AP_3C505_HCR_HSF2) != 0u ? 2u : 0u;
  return (ap_3c505_sf_t)(high | low);
}

ap_3c505_sf_t ap_3c505_adapter_flags(const ap_3c505_t *card) {
  if (card == NULL) {
    return AP_3C505_SF_UNDEFINED;
  }
  const unsigned low = (card->acr & AP_3C505_ACR_ASF1) != 0u ? 1u : 0u;
  const unsigned high = (card->acr & AP_3C505_ACR_ASF2) != 0u ? 2u : 0u;
  return (ap_3c505_sf_t)(high | low);
}

void ap_3c505_set_host_flags(ap_3c505_t *card, ap_3c505_sf_t state) {
  if (card == NULL) {
    return;
  }
  card->hcr &= (uint8_t)~(AP_3C505_HCR_HSF1 | AP_3C505_HCR_HSF2);
  if (((unsigned)state & 1u) != 0u) {
    card->hcr |= AP_3C505_HCR_HSF1;
  }
  if (((unsigned)state & 2u) != 0u) {
    card->hcr |= AP_3C505_HCR_HSF2;
  }
}

void ap_3c505_set_adapter_flags(ap_3c505_t *card, ap_3c505_sf_t state) {
  if (card == NULL) {
    return;
  }
  card->acr &= (uint8_t)~(AP_3C505_ACR_ASF1 | AP_3C505_ACR_ASF2);
  if (((unsigned)state & 1u) != 0u) {
    card->acr |= AP_3C505_ACR_ASF1;
  }
  if (((unsigned)state & 2u) != 0u) {
    card->acr |= AP_3C505_ACR_ASF2;
  }
}

void ap_3c505_pcb_rx_reset(ap_3c505_pcb_rx_t *rx) {
  if (rx == NULL) {
    return;
  }
  *rx = (ap_3c505_pcb_rx_t){0};
}

void ap_3c505_pcb_rx_byte(ap_3c505_pcb_rx_t *rx, uint8_t byte) {
  if (rx == NULL) {
    return;
  }
  rx->buffer[rx->written % AP_3C505_PCB_MAX] = byte;
  rx->written++;
}

uint8_t ap_3c505_pcb_total_length(const ap_3c505_pcb_t *pcb) {
  if (pcb == NULL) {
    return 0u;
  }
  /* "the TOTAL length of the PCB (excluding this byte)" -- the command code and
   * the length field are two bytes, and the data field is what the length
   * field counts. */
  return (uint8_t)(pcb->length + 2u);
}

bool ap_3c505_pcb_rx_end(const ap_3c505_pcb_rx_t *rx, ap_3c505_pcb_t *pcb) {
  if (rx == NULL || rx->written == 0u) {
    return false;
  }
  /* The last byte fed is the total length, and it is not part of the PCB. */
  const unsigned total = rx->buffer[(rx->written - 1u) % AP_3C505_PCB_MAX];
  if (total < 2u || total > AP_3C505_PCB_MAX) {
    /* Shorter than a bare header, or longer than the buffer that holds it. */
    return false;
  }
  if ((unsigned)total + 1u > rx->written) {
    /* The stream is shorter than the total claims, so the bytes it names were
     * never sent -- which is the aborted-transfer case this check exists for
     * rather than a buffer that merely has not wrapped. */
    return false;
  }
  /* "so the true beginning of PCB can be calculated": count back from the byte
   * before the total. */
  const unsigned start = rx->written - 1u - total;

  ap_3c505_pcb_t out = {0};
  out.command = rx->buffer[start % AP_3C505_PCB_MAX];
  out.length = rx->buffer[(start + 1u) % AP_3C505_PCB_MAX];
  if ((unsigned)out.length + 2u != total) {
    /* The data-length field and the total disagree, so these bytes are not one
     * PCB however plausible each is alone. */
    return false;
  }
  if (out.length > AP_3C505_PCB_LENGTH_MAX) {
    return false;
  }
  for (unsigned i = 0; i < out.length; i++) {
    out.data[i] = rx->buffer[(start + 2u + i) % AP_3C505_PCB_MAX];
  }
  if (pcb != NULL) {
    *pcb = out;
  }
  return true;
}

void ap_3c505_pcb_tx_start(ap_3c505_pcb_tx_t *tx, const ap_3c505_pcb_t *pcb) {
  if (tx == NULL || pcb == NULL) {
    return;
  }
  tx->pcb = *pcb;
  if (tx->pcb.length > AP_3C505_PCB_LENGTH_MAX) {
    tx->pcb.length = AP_3C505_PCB_LENGTH_MAX;
  }
  tx->sent = 0;
  tx->active = true;
}

bool ap_3c505_pcb_tx_next(ap_3c505_pcb_tx_t *tx, uint8_t *byte) {
  if (tx == NULL || !tx->active) {
    return false;
  }
  uint8_t value = 0;
  if (tx->sent == 0u) {
    value = tx->pcb.command;
  } else if (tx->sent == 1u) {
    value = tx->pcb.length;
  } else {
    const unsigned index = tx->sent - 2u;
    if (index >= tx->pcb.length) {
      tx->active = false;
      return false;
    }
    value = tx->pcb.data[index];
  }
  tx->sent++;
  if (tx->sent >= (unsigned)tx->pcb.length + 2u) {
    tx->active = false;
  }
  if (byte != NULL) {
    *byte = value;
  }
  return true;
}

void ap_3c505_adapter_init(ap_3c505_adapter_t *adapter,
                           const uint8_t address[AP_3C505_ADDRESS_BYTES]) {
  if (adapter == NULL) {
    return;
  }
  *adapter = (ap_3c505_adapter_t){0};
  if (address != NULL) {
    for (unsigned i = 0; i < AP_3C505_ADDRESS_BYTES; i++) {
      adapter->address[i] = address[i];
    }
  }
}

/* The PCB data field is little-endian: the manual writes its multi-byte fields
 * as `dw` and `dd`, which are 8086 directives, and the adapter is an 80186. */
static uint16_t read_word(const uint8_t *data) {
  return (uint16_t)((unsigned)data[0] | ((unsigned)data[1] << 8));
}

static void write_word(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value & 0xFFu);
  data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_dword(uint8_t *data, uint32_t value) {
  write_word(data, (uint16_t)(value & 0xFFFFu));
  write_word(data + 2, (uint16_t)((value >> 16) & 0xFFFFu));
}

/* §3.2.2's status word: "0 = successful", and it is a word in every response
 * that carries one. */
static void respond_status(ap_3c505_pcb_t *out, uint8_t code, uint16_t status) {
  out->command = code;
  out->length = 2u;
  write_word(out->data, status);
}

bool ap_3c505_dispatch(ap_3c505_adapter_t *adapter, const ap_3c505_pcb_t *in,
                       ap_3c505_pcb_t *out) {
  if (adapter == NULL || in == NULL || out == NULL) {
    return false;
  }
  *out = (ap_3c505_pcb_t){0};

  switch (in->command) {
  case AP_3C505_CMD_CONFIGURE_ADAPTER_MEMORY:
    /* "The Adapter allocates memory for the PCB command queue, receive command
     * queue, multicast address list, 82586 frame descriptors, receive buffers,
     * and download program data structures." Nothing in that allocation is
     * observable through the interface this core models -- the sizes govern how
     * much the adapter can queue, and a model with no queue depth to exhaust
     * accepts any of them. The response is the confirmation §3.2.2 specifies. */
    respond_status(out, 0x31u, 0u);
    return true;

  case AP_3C505_CMD_CONFIGURE_82586:
    if (in->length < 2u) {
      return false;
    }
    adapter->receive_mode = read_word(in->data);
    respond_status(out, 0x32u, 0u);
    return true;

  case AP_3C505_CMD_GET_ETHERNET_ADDRESS:
    /* §3.2.2 `33H`: "The Adapter returns the 6-byte Ethernet address ...
     * previously read from the Ethernet address PROM." */
    out->command = 0x33u;
    out->length = AP_3C505_ADDRESS_BYTES;
    for (unsigned i = 0; i < AP_3C505_ADDRESS_BYTES; i++) {
      out->data[i] = adapter->address[i];
    }
    return true;

  case AP_3C505_CMD_SET_ETHERNET_ADDRESS:
    if (in->length < AP_3C505_ADDRESS_BYTES) {
      return false;
    }
    for (unsigned i = 0; i < AP_3C505_ADDRESS_BYTES; i++) {
      adapter->address[i] = in->data[i];
    }
    respond_status(out, 0x40u, 0u);
    return true;

  case AP_3C505_CMD_LOAD_MULTICAST_LIST: {
    /* "A zero length list will cause the Adapter to clear all multicast
     * addresses", and the list is six bytes an entry, ten at most in one PCB. */
    const unsigned count = in->length / AP_3C505_ADDRESS_BYTES;
    if (in->length % AP_3C505_ADDRESS_BYTES != 0u ||
        count > AP_3C505_MULTICAST_MAX) {
      return false;
    }
    adapter->multicast_count = count;
    for (unsigned entry = 0; entry < count; entry++) {
      for (unsigned i = 0; i < AP_3C505_ADDRESS_BYTES; i++) {
        adapter->multicast[entry][i] =
            in->data[entry * AP_3C505_ADDRESS_BYTES + i];
      }
    }
    respond_status(out, 0x3Bu, 0u);
    return true;
  }

  case AP_3C505_CMD_NETWORK_STATISTICS:
    /* §3.2.2 `3AH`, in its order and widths: two long counters then four word
     * ones. "The Adapter clears all statistics after sending the response." */
    out->command = 0x3Au;
    out->length = 0x0Cu;
    write_dword(out->data, adapter->receive_packets);
    write_dword(out->data + 4, adapter->transmit_packets);
    write_word(out->data + 8, adapter->crc_errors);
    write_word(out->data + 10, adapter->alignment_errors);
    /* The last two of §3.2.2's six counters do not fit the `0CH` the manual
     * gives for the data field -- 4+4+2+2+2+2 is 16, not 12 -- so the length is
     * the manual's and the two that overflow it are not written. Recorded here
     * rather than resolved, because inventing a longer PCB to make the list fit
     * would be inventing a format. */
    adapter->receive_packets = 0;
    adapter->transmit_packets = 0;
    adapter->crc_errors = 0;
    adapter->alignment_errors = 0;
    adapter->no_resource_errors = 0;
    adapter->overrun_errors = 0;
    return true;

  case AP_3C505_CMD_DOWNLOAD_DATA_DMA:
  case AP_3C505_CMD_UPLOAD_DATA_DMA:
  case AP_3C505_CMD_DOWNLOAD_DATA_PIO:
  case AP_3C505_CMD_UPLOAD_DATA_PIO:
    /* "There is no adapter response PCB for this command." */
    return false;

  default:
    /* Everything whose response format this project has not read. Refusing is
     * the protocol's own answer -- §3.1.1's state `10` -- and is honest where a
     * made-up payload would not be. */
    return false;
  }
}
