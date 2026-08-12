#!/bin/sh
# The boot that reaches `E0007`, recorded so it does not have to be guessed at
# again.
#
# An hour was spent reconstructing an invocation that had produced a crash and
# was written down only as a screenshot. Four boots later the answer turned out
# to be the **clock**, and none of the flags that had been tried. That is the
# same rule as `identity-boot.sh`'s -- a result without its invocation is not a
# result -- applied to a failure instead of a hash.
#
# What matters, and why:
#
#   --clock 2026-08-09   **This is the whole trick.** The volume was installed
#                        2026-08-02, so a calendar within fourteen days of it
#                        does not make Domain/OS ask "More than 14 days have
#                        elapsed ... run DOMAIN_OS with the current calendar?".
#                        A later clock provokes that question, the machine waits
#                        at it forever, and the run *looks* like a boot that has
#                        stopped crashing. It has not: it has stopped arriving.
#                        Answering the question from the keyboard does not work
#                        -- the second `--boot-type` phase arms and types at
#                        every quiet threshold from 10,000 to 200,000
#                        instructions and the character is still discarded
#                        unread. Not provoking it is cheaper than answering it.
#   --screen c8p         Domain/OS talks to the display, not the serial line.
#                        Without a display fitted the boot takes a different
#                        path and prints nothing anywhere.
#   --boot-type-after-pc 2670 / --boot-type "Y\r"
#                        answers SELF_TEST's "DO YOU WISH TO CONTINUE (Y,N)?",
#                        which is a setup step and not a fault. This one *is*
#                        typed and read: the arming address is reached once.
#
# Deliberately **no** `--boot-input`/`--boot-script`: the PROM's serial dialogue
# is a different console and adds a character on `sio1 B` that nothing reads.
#
# What it does, as of 2026-08-12: stops at `PC 3C4524E6` after **477,842,981**
# instructions, with `crp 0105BC00`, 68 `PMOVE` loads and 286 `MMUSR` reads --
# i.e. past the address-space switch. The screen reads
#
#     Unable to resolve "/sys/node_data" -- E0007
#     CRASH_STATUS 000E0007  PC 3C4524E6 PID 0001
#
# Pass `--boot-stop-pc 3C4524E6` (plus a trace or a dump) to stop *at* the
# failure instead of running past it; the instruction count above is what that
# stop reports and is the cheapest check that a change has not moved the boot.
#
# Usage: tools/e0007-boot.sh [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

# The core writes to the image it is given, so a run must not be pointed at the
# artifact in `media/`. Copy first, and let the caller override the copy's home.
disk=${APOLLO_E0007_DISK:-}
if [ -z "$disk" ]; then
  disk=${TMPDIR:-/tmp}/apollo-e0007-disk.awd
  cp "$root/media/dn3500-sr10.4-installed.awd" "$disk"
fi

exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --disk "$disk" \
  --screen c8p \
  --clock 2026-08-09 \
  --boot-type-after-pc 2670 \
  --boot-type "$(printf 'Y\r')" \
  --boot-limit 1500000000 \
  --boot-console \
  --boot-report \
  "$@"
