/* A host network interface for an emulated Ethernet card, over Linux TAP.
 *
 * ## Why this is in a frontend and not in the core
 *
 * `src/core` knows nothing about frontends, and a socket is the sharpest case
 * of that rule: `ap_3c505_wire_t` is a context and a `transmit` callback, and
 * the device model cannot tell a real interface from a capture file. This is
 * one implementation of that callback; a replay backend would be another, and
 * the card would not know.
 *
 * ## What attaching this costs, stated rather than discovered
 *
 * **A run with a live wire is not deterministic and must not be hashed.**
 * `CLAUDE.md` requires the headless frontend to have no wall clock, no host
 * input and no threads, and the identity harness is a promise that the same
 * invocation produces the same state hash on every machine and build type.
 * Frames arriving from a real network arrive at instants nothing in the
 * emulation controls, so a run with TAP attached breaks that promise by
 * construction -- not by a bug that could be fixed.
 *
 * The rule that follows is therefore mechanical: **a TAP-attached run may not
 * report a state hash and may not be used for timing.** The frontend enforces
 * it rather than documenting it, because a determinism claim that depends on
 * the operator remembering is not one.
 *
 * A capture backend is the deterministic alternative and is the one to build
 * for tests: frames delivered at emulated-time offsets from a file are
 * reproducible, hashable, and still real traffic.
 *
 * ## Privilege
 *
 * `/dev/net/tun` needs `CAP_NET_ADMIN` to create an interface, which an
 * ordinary user does not have. The usual arrangement is a persistent device
 * created once by an administrator and owned by the user:
 *
 *     sudo ip tuntap add dev apollo0 mode tap user "$USER"
 *     sudo ip addr add 10.0.2.1/24 dev apollo0
 *     sudo ip link set apollo0 up
 *
 * Then `--3c505-tap apollo0` attaches to it without privilege. Open failures
 * are reported with the reason rather than swallowed: "no such device" and
 * "permission denied" are different problems with different fixes, and a
 * backend that answered "no network" to both would hide which.
 */

#ifndef APOLLO_FRONTEND_COMMON_AP_TAP_H
#define APOLLO_FRONTEND_COMMON_AP_TAP_H

#include <stdbool.h>
#include <stdint.h>

#include "device/ap_3c505.h"

#define AP_TAP_NAME_MAX 16u

typedef struct {
  int fd;
  char name[AP_TAP_NAME_MAX];
  uint64_t frames_in;
  uint64_t frames_out;
  uint64_t dropped_in;  /* arrived with no receive armed, or oversize */
  uint64_t failed_out;  /* the write refused, so the frame did not go */
} ap_tap_t;

/* Attach to an existing TAP device. Returns false and fills `error` with a
 * sentence naming the cause when it cannot. */
[[nodiscard]] bool ap_tap_open(ap_tap_t *tap, const char *name, char *error,
                               unsigned error_size);

void ap_tap_close(ap_tap_t *tap);

/* The wire an `ap_3c505_adapter_t` transmits through. */
[[nodiscard]] ap_3c505_wire_t ap_tap_wire(ap_tap_t *tap);

/* Poll for one frame and deliver it to the adapter. Returns true when a frame
 * was delivered, at which point `out` is the `38H` response the host is owed.
 * Non-blocking: no frame waiting is the common case and is not an error. */
[[nodiscard]] bool ap_tap_poll(ap_tap_t *tap, ap_3c505_adapter_t *adapter,
                               ap_3c505_pcb_t *out);

#endif /* APOLLO_FRONTEND_COMMON_AP_TAP_H */
