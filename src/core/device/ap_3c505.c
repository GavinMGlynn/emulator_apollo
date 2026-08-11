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
