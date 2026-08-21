#!/bin/sh
# The boot that reaches a **running** Domain/OS -- `SPM system init complete.`,
# `Node ID`, `siomonit` started -- recorded so it does not have to be guessed at
# again.
#
# It exists for the same reason `identity-boot.sh` and `e0007-boot.sh` do, and
# for one more: **four long runs (about 90 minutes) were spent reaching this
# state and none of them arrived**, because the invocation was assembled from
# the other two scripts and inherited the wrong calendar from one of them. A
# harness that reaches a state is the cheapest thing in this project to record
# and the most expensive thing to rebuild.
#
# What matters, and why:
#
#   --clock 2002-11-28   **This is the whole trick, and it is not the clock
#                        `e0007-boot.sh` uses.** Domain/OS compares the calendar
#                        against the volume's own recorded dismount stamp, and
#                        *every* volume this project owns is stamped
#                        **2002-11-27/28** -- read it with
#                        `apollo-headless --volume FILE` rather than from the
#                        host file's date. A 2026 calendar is twenty-four years
#                        past the stamp, so the kernel asks "More than 14 days
#                        have elapsed ... run DOMAIN_OS with the current
#                        calendar?" and waits at it for ever. That is not a boot
#                        that failed to reach SPM; it is a boot that stopped
#                        arriving, and the two are indistinguishable from a
#                        `grep -c` that returns zero.
#   the serial dialogue  `--boot-input $'\r'` on port 1 channel B plus
#                        `--boot-script`, exactly as `identity-boot.sh` has
#                        them: the PROM autobauds and cannot transmit until it
#                        has received a character, and the `y` must come from
#                        the script, which waits for the prompt.
#   **no** --screen      a key press selects the *display* as the operating
#                        system's console, and `--boot-console` then prints
#                        nothing at all. With no display fitted the console is
#                        serial 1 channel B and the whole startup arrives on
#                        stdout. `--boot-console` drains all four channels, so a
#                        `login:` offered on **sio2** lands here too.
#
# Where it gets to, measured 2026-08-21 on three volumes: `Domain/OS kernel(7),
# revision 10.4`, `Apollo Phase II Environment`, `Loading Init`, global
# libraries, `Initializing /etc/mnttab`, standard daemons, `SPM system init
# complete.`, `Node ID`, the `siomonit` CPS line and `MBX_HELPER`. **SPM lands
# at about 1.05 G instructions**, not the 2.6 G `PROJECT_STATUS.md` recorded, so
# a 1.5 G bound already covers it and anything longer is buying post-SPM idle
# time rather than reach.
#
# **Run on a copy.** The core writes to the image it is given; pointing a run at
# `media/` mutates the artifact. This script does not copy for you, because
# which volume is the question the caller is asking.
#
# Usage: tools/spm-boot.sh <disk-image-copy> [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

if [ $# -lt 1 ]; then
  echo "usage: tools/spm-boot.sh <disk-image-copy> [args...]" >&2
  exit 2
fi
disk=$1
shift

exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --disk "$disk" \
  --boot-console \
  --boot-input "$(printf '\r')" \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-script "$root/tools/boot-domainos.script" \
  --clock 2002-11-28 \
  --boot-limit 1500000000 \
  --boot-report \
  "$@"
