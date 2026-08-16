/* See `ap_ring_link.h` for why the batch size is a cable length and why this
 * lives in the frontend rather than the core. */

#include "ap_ring_link.h"

#include <string.h>

/* ## Sockets on three platforms, with no third-party carrier
 *
 * `<sys/socket.h>` and `<unistd.h>` are not Windows headers, and an unguarded
 * include turned the whole tree red on the Windows job while the other three
 * were green. The answer is **Winsock2**, which ships with the operating
 * system: no dependency to vendor, nothing to pin in `ext/`, and no licence
 * carve-out to add for a facility the platform already provides.
 *
 * The port is small because the format is bytes and the lock-step is not
 * POSIX-specific. `send` and `recv` exist on **both** platforms with the same
 * shape, so they are used everywhere rather than `write`/`read`, and what is
 * left differs in four places only:
 *
 *   - the headers, and `WSAStartup`, which Winsock requires before any call
 *     and which is done once, lazily, on the first `init`;
 *   - the descriptor: Windows `SOCKET` is unsigned and its invalid value is
 *     `INVALID_SOCKET`, not `-1`. The public type stays `int` because a caller
 *     lends a descriptor it already owns, and the check is `< 0` either way on
 *     the platforms this builds for;
 *   - the interrupted-call code, `WSAEINTR` against `EINTR`;
 *   - `MSG_NOSIGNAL`, which exists to stop a closed peer raising `SIGPIPE` and
 *     killing the process. Windows raises no such signal, so the flag is
 *     absent there and needed nowhere else.
 *
 * `ap_ring_link_available` still exists and is now true on every supported
 * platform. It stays because a build *could* lack a carrier -- and because a
 * caller reporting "this build has none" is better than one discovering it
 * from a ring that never forms.
 */
#if defined(_WIN32)
#define AP_RING_LINK_HAVE 1
#include <winsock2.h>
#define AP_LINK_EINTR WSAEINTR
#define ap_link_errno() WSAGetLastError()
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#define AP_RING_LINK_HAVE 1
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#define AP_LINK_EINTR EINTR
#define ap_link_errno() errno
#endif

/* Winsock refuses every call until the library is started, and starting it
 * twice is harmless but leaking the startup is not the frontend's business --
 * a process that opened a ring link keeps it for its lifetime, so this is done
 * once and never torn down. On POSIX it compiles to nothing. */
static bool link_platform_ready(void) {
#if defined(_WIN32)
  static bool started = false;
  if (!started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      return false;
    }
    started = true;
  }
#endif
  return true;
}

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
 * link. Windows raises no such signal and defines the flag as zero here, so the
 * same call is correct on both. */
/* `send`/`recv` take a `size_t` length on POSIX and an `int` on Winsock, so
 * the cast is platform-specific rather than one or the other -- `-Wconversion`
 * catches the wrong choice, which is how this was found. A batch is at most 64
 * bytes, so neither type can lose anything. */
#if defined(_WIN32)
typedef int ap_link_len_t;
/* And the descriptor itself: Windows `SOCKET` is `unsigned long long`, so
 * handing it an `int` is a signedness conversion that `-Wconversion` rejects.
 * The public type stays `int` -- a caller lends a descriptor it already owns,
 * and `-1`/`INVALID_SOCKET` both fail the `< 0` check on the platforms this
 * builds for -- so the widening happens here, at the two calls that need it. */
typedef SOCKET ap_link_sock_t;
#else
typedef size_t ap_link_len_t;
typedef int ap_link_sock_t;
#endif

static long link_send_once(int fd, const uint8_t *bytes, size_t len) {
  return (long)send((ap_link_sock_t)fd, (const char *)bytes,
                    (ap_link_len_t)len, MSG_NOSIGNAL);
}

static long link_recv_once(int fd, uint8_t *bytes, size_t len) {
  return (long)recv((ap_link_sock_t)fd, (char *)bytes, (ap_link_len_t)len, 0);
}

static bool write_all(int fd, const uint8_t *bytes, size_t len) {
  size_t done = 0u;
  while (done < len) {
    const long n = link_send_once(fd, bytes + done, len - done);
    if (n < 0) {
      if (ap_link_errno() == AP_LINK_EINTR) {
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
    const long n = link_recv_once(fd, bytes + done, len - done);
    if (n < 0) {
      if (ap_link_errno() == AP_LINK_EINTR) {
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
      cable_bits > AP_RING_MAX_CABLE_BITS || !link_platform_ready()) {
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
