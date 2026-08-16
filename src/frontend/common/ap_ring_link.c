/* See `ap_ring_link.h` for why the batch size is a cable length and why this
 * lives in the frontend rather than the core. */

#include "ap_ring_link.h"

#include <string.h>

/* ## POSIX sockets, or a build that says it has none
 *
 * `<sys/socket.h>` and `<unistd.h>` are not Windows headers, and this project
 * builds on three platforms with `-Werror` -- so an unguarded include turned
 * the whole tree red on the Windows job while the other three were green. The
 * shape of the fix is `ap_png.c`'s: **compile either way and report which build
 * it is**, rather than dropping the file from one platform's target list, where
 * the absence is invisible until someone wonders why a flag does nothing.
 *
 * Winsock would work here -- the wire format is bytes and the lock-step is not
 * POSIX-specific -- and is deliberately not written blind: this project has no
 * Windows machine to run it on, and an untested carrier that silently
 * desynchronises two rings is worse than one that refuses. `AP_RING_LINK_HAVE`
 * says which build this is. */
#if defined(_WIN32)
#define AP_RING_LINK_HAVE 0
#else
#define AP_RING_LINK_HAVE 1
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if AP_RING_LINK_HAVE

/* One byte per bit time: bit 0 the clock window, bit 1 the data window. The
 * two spare bits are left zero and *checked* on the way in -- a peer speaking a
 * different version of this format fails immediately rather than delivering
 * cells whose extra bits were quietly discarded. */
#define LINK_CLOCK 0x01u
#define LINK_DATA 0x02u
#define LINK_RESERVED 0xFCu

/* `write` and `read` may both do less than asked on a socket, and a partial
 * transfer here is not an error -- it is the normal case under load. Looping is
 * what makes the exchange lock-step rather than best-effort; a version that
 * treated a short read as "the peer sent less this batch" would desynchronise
 * the two rings by however many bit times went missing, and neither side would
 * see anything but a ring that stopped forming. `EINTR` is retried for the same
 * reason: a signal is not a message. */
/* One write, and **it must not kill the emulator when the peer has gone**.
 *
 * A plain `write` to a socket whose far end is closed raises `SIGPIPE`, whose
 * default action terminates the process -- so a node leaving the ring would
 * take the surviving emulator down with it, mid-boot, with no diagnostic. That
 * is not a hypothetical: it is what this suite did, exiting 141 before a single
 * assertion in the test that closes the peer.
 *
 * `MSG_NOSIGNAL` asks for `EPIPE` instead, which the caller turns into a failed
 * link. It is a socket flag, so a caller who lent a *pipe* rather than a socket
 * falls back to `write` -- and on that path the signal is theirs to mask, which
 * the header says. */
static ssize_t write_once(int fd, const uint8_t *bytes, size_t len) {
#ifdef MSG_NOSIGNAL
  const ssize_t n = send(fd, bytes, len, MSG_NOSIGNAL);
  if (n >= 0 || errno != ENOTSOCK) {
    return n;
  }
#endif
  return write(fd, bytes, len);
}

static bool write_all(int fd, const uint8_t *bytes, size_t len) {
  size_t done = 0u;
  while (done < len) {
    const ssize_t n = write_once(fd, bytes + done, len - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    done += (size_t)n;
  }
  return true;
}

static bool read_all(int fd, uint8_t *bytes, size_t len) {
  size_t done = 0u;
  while (done < len) {
    const ssize_t n = read(fd, bytes + done, len - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false; /* the peer closed: a node left the ring */
    }
    done += (size_t)n;
  }
  return true;
}

bool ap_ring_link_init(ap_ring_link_t *link, int fd, unsigned cable_bits) {
  if (link == NULL || fd < 0 || cable_bits == 0u ||
      cable_bits > AP_RING_MAX_CABLE_BITS) {
    return false;
  }
  link->fd = fd;
  link->cable_bits = cable_bits;
  link->failed = false;
  link->batches = 0u;
  return true;
}

bool ap_ring_link_send(ap_ring_link_t *link, const ap_ring_cell_t *local) {
  if (link == NULL || link->failed || local == NULL) {
    return false;
  }
  uint8_t out[AP_RING_MAX_CABLE_BITS];
  for (unsigned i = 0; i < link->cable_bits; i++) {
    out[i] = (uint8_t)((local[i].clock_window ? LINK_CLOCK : 0u) |
                       (local[i].data_window ? LINK_DATA : 0u));
  }
  if (!write_all(link->fd, out, link->cable_bits)) {
    link->failed = true;
    return false;
  }
  return true;
}

bool ap_ring_link_recv(ap_ring_link_t *link, ap_ring_cell_t *remote) {
  if (link == NULL || link->failed || remote == NULL) {
    return false;
  }
  uint8_t in[AP_RING_MAX_CABLE_BITS];
  if (!read_all(link->fd, in, link->cable_bits)) {
    link->failed = true;
    return false;
  }
  for (unsigned i = 0; i < link->cable_bits; i++) {
    if ((in[i] & LINK_RESERVED) != 0u) {
      /* A peer speaking a different format. Refused rather than masked off:
       * silently dropping bits it thought it was sending is how two emulators
       * end up modelling different cables and blaming the ring. */
      link->failed = true;
      return false;
    }
    remote[i].clock_window = (in[i] & LINK_CLOCK) != 0u;
    remote[i].data_window = (in[i] & LINK_DATA) != 0u;
  }
  link->batches++;
  return true;
}

/* Both ends send before either receives, which two concurrent processes do
 * naturally. A single thread driving both ends must use the halves. */
bool ap_ring_link_exchange(ap_ring_link_t *link, const ap_ring_cell_t *local,
                           ap_ring_cell_t *remote) {
  return ap_ring_link_send(link, local) && ap_ring_link_recv(link, remote);
}

#else /* !AP_RING_LINK_HAVE */

bool ap_ring_link_init(ap_ring_link_t *link, int fd, unsigned cable_bits) {
  (void)fd;
  (void)cable_bits;
  if (link != NULL) {
    *link = (ap_ring_link_t){0};
    link->failed = true;
  }
  return false;
}

bool ap_ring_link_send(ap_ring_link_t *link, const ap_ring_cell_t *local) {
  (void)link;
  (void)local;
  return false;
}

bool ap_ring_link_recv(ap_ring_link_t *link, ap_ring_cell_t *remote) {
  (void)link;
  (void)remote;
  return false;
}

bool ap_ring_link_exchange(ap_ring_link_t *link, const ap_ring_cell_t *local,
                           ap_ring_cell_t *remote) {
  (void)link;
  (void)local;
  (void)remote;
  return false;
}

#endif /* AP_RING_LINK_HAVE */

bool ap_ring_link_available(void) { return AP_RING_LINK_HAVE != 0; }
