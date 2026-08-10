#include "model/ap_quirk.h"

#include <string.h>

/* One row per quirk, and the row is the argument for having it.
 *
 * `reference` is what a document says, with the document named. `oracle` is
 * what MAME does instead, with the file and function named so the claim can be
 * checked in five seconds. A row that cannot fill both fields is not a quirk --
 * it is an open question, and belongs in the findings file until it is settled.
 */
static const struct {
  const char *name;
  const char *description;
} QUIRKS[AP_QUIRK_COUNT] = {
    [AP_QUIRK_GRAPHICS_ID_ALWAYS_COLOUR] =
        {"graphics-id-always-colour",
         "the colour display block answers its ID whatever board is fitted "
         "(MAME apollo_v.cpp: `data = m_n_planes == 1 ? m_device_id : 0xff` on "
         "a device whose plane count does not follow the Graphics Controller "
         "setting), where this core answers a block's ID only for its own "
         "family -- the firmware reads both blocks at 0069AA to discover what "
         "is installed, so answering from the wrong one sends it down the "
         "colour path on a monochrome machine"},
};

const char *ap_quirk_name(ap_quirk_t quirk) {
  return (unsigned)quirk < AP_QUIRK_COUNT ? QUIRKS[quirk].name : "";
}

const char *ap_quirk_description(ap_quirk_t quirk) {
  return (unsigned)quirk < AP_QUIRK_COUNT ? QUIRKS[quirk].description : "";
}

bool ap_quirk_by_name(const char *name, ap_quirk_t *out) {
  if (name == NULL || out == NULL) {
    return false;
  }
  for (unsigned i = 0; i < AP_QUIRK_COUNT; i++) {
    if (QUIRKS[i].name != NULL && strcmp(QUIRKS[i].name, name) == 0) {
      *out = (ap_quirk_t)i;
      return true;
    }
  }
  return false;
}
