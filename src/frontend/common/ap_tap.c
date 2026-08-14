/* Linux TAP as an Ethernet wire. See ap_tap.h for why this is not in the core
 * and what attaching it costs. */

#include "ap_tap.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* **The POSIX headers are behind the guard, not beside it.** `unistd.h` does
 * not exist under MSVC, and CI builds there: an implementation guarded at the
 * function while its includes were unconditional compiled on two of three
 * platforms and failed the third at the `#include`, which is a red tree for a
 * device nobody on that platform can use. */
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* `-Wformat-nonliteral` is on, and rightly: a format string that is not a
 * literal is how a message becomes an injection. The attribute tells the
 * compiler this *is* a printf, so it checks the callers' literals instead. */
__attribute__((format(printf, 3, 4))) static void say(char *error,
                                                      unsigned size,
                                                      const char *fmt, ...) {
  if (error == NULL || size == 0u) {
    return;
  }
  va_list args;
  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  (void)vsnprintf(error, size, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);
}

#if !defined(__linux__)

/* A platform without TAP says so, rather than silently answering "no network":
 * a cable that is unplugged and an interface that cannot exist are different
 * facts, and an operator debugging the first should not be shown the second.
 * Every entry point is stubbed so the frontend links and behaves identically
 * to one whose device failed to open. */

bool ap_tap_open(ap_tap_t *tap, const char *name, char *error,
                 unsigned error_size) {
  (void)tap;
  (void)name;
  say(error, error_size, "TAP is a Linux interface and this is not Linux");
  return false;
}

void ap_tap_close(ap_tap_t *tap) { (void)tap; }

static bool tap_transmit(void *context, const uint8_t *frame, unsigned length) {
  (void)context;
  (void)frame;
  (void)length;
  return false;
}

ap_3c505_wire_t ap_tap_wire(ap_tap_t *tap) {
  ap_3c505_wire_t wire = {0};
  wire.context = tap;
  wire.transmit = tap_transmit;
  return wire;
}

bool ap_tap_poll(ap_tap_t *tap, ap_3c505_adapter_t *adapter,
                 ap_3c505_pcb_t *out) {
  (void)tap;
  (void)adapter;
  (void)out;
  return false;
}

#else

bool ap_tap_open(ap_tap_t *tap, const char *name, char *error,
                 unsigned error_size) {
  if (tap == NULL || name == NULL) {
    say(error, error_size, "no device name given");
    return false;
  }
  memset(tap, 0, sizeof *tap);
  tap->fd = -1;

  if (strlen(name) >= AP_TAP_NAME_MAX) {
    say(error, error_size, "device name '%s' is longer than %u characters",
        name, AP_TAP_NAME_MAX - 1u);
    return false;
  }

  const int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    say(error, error_size, "cannot open /dev/net/tun: %s", strerror(errno));
    return false;
  }

  struct ifreq request;
  memset(&request, 0, sizeof request);
  /* `IFF_TAP` is an Ethernet device -- whole frames with their headers -- where
   * `IFF_TUN` would be IP packets without them. The card transmits frames, so
   * anything else would need this layer to invent the header it strips.
   * `IFF_NO_PI` drops the four-byte packet-information prefix the kernel would
   * otherwise prepend, which is not part of the frame and would be transmitted
   * as if it were. */
  request.ifr_flags = (short)(IFF_TAP | IFF_NO_PI);
  (void)strncpy(request.ifr_name, name, IFNAMSIZ - 1u);

  if (ioctl(fd, TUNSETIFF, &request) < 0) {
    const int failure = errno;
    (void)close(fd);
    if (failure == EPERM) {
      say(error, error_size,
          "not permitted to attach to '%s': create it persistently first, "
          "e.g. `sudo ip tuntap add dev %s mode tap user $USER`",
          name, name);
    } else {
      say(error, error_size, "cannot attach to '%s': %s", name,
          strerror(failure));
    }
    return false;
  }

  tap->fd = fd;
  (void)strncpy(tap->name, name, AP_TAP_NAME_MAX - 1u);
  return true;
}

void ap_tap_close(ap_tap_t *tap) {
  if (tap == NULL || tap->fd < 0) {
    return;
  }
  (void)close(tap->fd);
  tap->fd = -1;
}

static bool tap_transmit(void *context, const uint8_t *frame, unsigned length) {
  ap_tap_t *tap = (ap_tap_t *)context;
  if (tap == NULL || tap->fd < 0 || frame == NULL || length == 0u) {
    return false;
  }
  const ssize_t written = write(tap->fd, frame, (size_t)length);
  if (written < 0 || (unsigned)written != length) {
    /* A short write is a failed transmission, not a partial one: half a frame
     * on the wire is not a frame, and the adapter's completion status is where
     * a driver is told. */
    tap->failed_out++;
    return false;
  }
  tap->frames_out++;
  return true;
}

ap_3c505_wire_t ap_tap_wire(ap_tap_t *tap) {
  ap_3c505_wire_t wire = {0};
  wire.context = tap;
  wire.transmit = tap_transmit;
  return wire;
}

bool ap_tap_poll(ap_tap_t *tap, ap_3c505_adapter_t *adapter,
                 ap_3c505_pcb_t *out) {
  if (tap == NULL || tap->fd < 0 || adapter == NULL) {
    return false;
  }
  uint8_t frame[AP_3C505_FRAME_MAX];
  const ssize_t got = read(tap->fd, frame, sizeof frame);
  if (got <= 0) {
    /* `EAGAIN` is the ordinary case on a quiet network and is not an error.
     * The descriptor is non-blocking precisely so that a machine with nothing
     * to receive does not stop. */
    return false;
  }
  tap->frames_in++;
  if (!ap_3c505_deliver_frame(adapter, frame, (unsigned)got, out)) {
    /* The adapter refused it -- no receive armed, which its own no-resources
     * counter records. Counted here too, because "the network is quiet" and
     * "the guest is not listening" look identical from outside and are
     * different problems. */
    tap->dropped_in++;
    return false;
  }
  return true;
}

#endif /* __linux__ */
