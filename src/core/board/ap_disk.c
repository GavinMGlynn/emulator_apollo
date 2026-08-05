#include "board/ap_disk.h"

void ap_disk_reset(ap_disk_t *disk) { ap_omti_reset(&disk->controller); }

bool ap_disk_decode(uint32_t address, bool *is_floppy, unsigned *reg) {
  if (address >= AP_DISK_FIXED_ADDR &&
      address < AP_DISK_FIXED_ADDR + AP_DISK_FIXED_SIZE) {
    *is_floppy = false;
    /* Four registers aliased every eight bytes, measured over the whole
     * kilobyte: offsets 1, 2 and 3 answered a read and offset 0 held `FF`. */
    *reg = (address - AP_DISK_FIXED_ADDR) & (AP_OMTI_DISK_REGISTERS - 1u);
    return true;
  }
  if (address >= AP_DISK_FLOPPY_ADDR &&
      address < AP_DISK_FLOPPY_ADDR + AP_DISK_FLOPPY_SIZE) {
    *is_floppy = true;
    /* An eight-address block, not four: the floppy's registers are spread
     * across offsets 2 and 4 to 7 rather than packed from zero. */
    *reg = (address - AP_DISK_FLOPPY_ADDR) & (AP_OMTI_FLOPPY_REGISTERS - 1u);
    return true;
  }
  return false;
}

bool ap_disk_is_data_port(const ap_disk_t *disk, uint32_t address) {
  (void)disk;
  bool is_floppy = false;
  unsigned reg = 0;
  return ap_disk_decode(address, &is_floppy, &reg) && !is_floppy &&
         reg == AP_OMTI_DISK_DATA;
}

uint16_t ap_disk_read16(ap_disk_t *disk, uint32_t address) {
  if (!ap_disk_is_data_port(disk, address)) {
    return 0xFFFFu;
  }
  /* One cycle, counted as one -- a word read of the data port is one access to
   * it and not two, which is the whole distinction this path exists to keep. */
  disk->disk_reads[AP_OMTI_DISK_DATA]++;
  return ap_omti_disk_read16(&disk->controller);
}

void ap_disk_write16(ap_disk_t *disk, uint32_t address, uint16_t value) {
  if (!ap_disk_is_data_port(disk, address)) {
    return;
  }
  disk->disk_writes[AP_OMTI_DISK_DATA]++;
  ap_omti_disk_write16(&disk->controller, value);
}

uint8_t ap_disk_read(ap_disk_t *disk, uint32_t address) {
  bool is_floppy;
  unsigned reg;
  if (!ap_disk_decode(address, &is_floppy, &reg)) {
    return 0xFFu;
  }
  /* Counted here rather than in the part: see the header. */
  if (is_floppy) {
    if (reg < AP_OMTI_FLOPPY_REGISTERS) { disk->floppy_reads[reg]++; }
    return ap_omti_fdc_read(&disk->controller, reg);
  }
  if (reg < AP_OMTI_DISK_REGISTERS) { disk->disk_reads[reg]++; }
  return ap_omti_disk_read(&disk->controller, reg);
}

void ap_disk_write(ap_disk_t *disk, uint32_t address, uint8_t value) {
  bool is_floppy;
  unsigned reg;
  if (!ap_disk_decode(address, &is_floppy, &reg)) {
    return;
  }
  if (is_floppy) {
    if (reg < AP_OMTI_FLOPPY_REGISTERS) { disk->floppy_writes[reg]++; }
    ap_omti_fdc_write(&disk->controller, reg, value);
  } else {
    if (reg < AP_OMTI_DISK_REGISTERS) { disk->disk_writes[reg]++; }
    const unsigned before = ap_omti_command_count(&disk->controller);
    ap_omti_disk_write(&disk->controller, reg, value);
    if (ap_omti_command_count(&disk->controller) != before) {
      ap_disk_note_command(disk, ap_omti_last_command(&disk->controller));
    }
  }
}

uint8_t ap_disk_dma_read(ap_disk_t *disk, bool is_floppy) {
  /* The data port, reached through the acknowledge instead of through an
   * address. Deferring to the same register call rather than reading the field
   * keeps the read/write asymmetries `[OMTI]` documents true of both routes. */
  return is_floppy ? ap_omti_fdc_read(&disk->controller, AP_OMTI_FDC_DATA)
                   : ap_omti_disk_read(&disk->controller, AP_OMTI_DISK_DATA);
}

void ap_disk_dma_write(ap_disk_t *disk, bool is_floppy, uint8_t value) {
  if (is_floppy) {
    ap_omti_fdc_write(&disk->controller, AP_OMTI_FDC_DATA, value);
  } else {
    ap_omti_disk_write(&disk->controller, AP_OMTI_DISK_DATA, value);
  }
}

void ap_disk_note_command(ap_disk_t *disk, uint8_t opcode) {
  disk->commands[opcode]++;
  disk->command_total++;
}

