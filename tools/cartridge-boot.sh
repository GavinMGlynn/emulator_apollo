#!/bin/sh
# The SR10.4 cartridge boot, recorded so it does not have to be reconstructed
# again.
#
# `PROJECT_STATUS.md`'s *The SR10.4 cartridge boots* prints this run's console,
# and `COMPLETION_PLAN.md`'s `E0007` item turns on it, and **neither carried the
# invocation** -- so the next person to measure the `E0007` search had to
# rebuild the command line from a transcript. That is the failure
# `tools/e0007-boot.sh` exists because of after it cost an hour, and the rule
# `tools/identity-boot.sh` states: a result without its invocation is not a
# result. Reconstructing it here cost three wrong runs, and every one of them
# was the same mistake -- guessing at the dialogue instead of reading the
# neighbouring script.
#
# **It is `tools/md-shell.sh` with a cartridge**, and that is the whole of it.
# The `>` in the recorded transcript is the **Mnemonic Debugger's** prompt, not
# the boot PROM's: the PROM never prints one. So this needs MD's knock, MD's
# rate and MD's entry address, all three copied from the script next door rather
# than rediscovered:
#
#   --service-mode          MD is not reachable without it.
#   --boot-input <knock>    120 carriage returns. MD is entered by knocking, and
#                           two characters are spent before MD exists to hear
#                           them.
#   --boot-input-after-pc 78E   where the knock is held for. **A disk-less
#                           machine left alone runs its self test, prints
#                           `Could not load /SAU7/SELF_TEST.` and sits at
#                           exactly this address** -- for 1.2 G instructions, in
#                           the run that established it. A machine "stuck" at
#                           `78E` is one waiting to be knocked at.
#   --boot-input-rate 0x88  2400 baud, and not a search: the PROM's own autobaud
#                           table maps a 2400 terminal's `0D` to CSR $88, so the
#                           negotiation closes. A 9600 terminal's is answered
#                           with 4800 and the third character is then misread.
#                           `FINDINGS.md` C240.
#   --clock 2002-11-28      the volume stamp every image this project owns
#                           carries. A 2026 calendar produces a *different*
#                           `E0007` -- the disk boot's `Unable to resolve
#                           "/sys/node_data"`, which `tools/e0007-boot.sh`
#                           reproduces and which was **retracted** as a core
#                           defect. The two must not be confused; this one is
#                           `can't find bscom/rbak_shell on tape`.
#
# **The disk is optional and off by default.** `EX DOMAIN_OS` loads the *tape's*
# SYSBOOT, so the cartridge is the whole *boot* device and `tools/md-shell.sh`
# needs a disk only because it says `DI W`. But the kernel probes a disk once it
# is running -- a diskless run reports `disk last 00, error, sense 04 00 00 00`
# -- so set `APOLLO_CARTRIDGE_DISK` to a **copy** of an image to give it one.
# Which of the two the recorded transcript used is the open question this
# script exists to settle; both are one run apart now instead of an hour.
#
# **No `--screen`.** A fitted display takes the firmware's output to the frame
# buffer and leaves the serial console silent. This boot is read on the wire.
#
# Usage: tools/cartridge-boot.sh [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

# The core writes to the image it is given, so a run must not be pointed at the
# artifact in `media/`. Copy first, and let the caller override the copy's home.
cartridge=${APOLLO_CARTRIDGE:-}
if [ -z "$cartridge" ]; then
  cartridge=${TMPDIR:-/tmp}/apollo-boot-cartridge.ct
  cp "$root/media/domainos/019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct" "$cartridge"
fi

knock=$(awk 'BEGIN { for (i = 0; i < 120; i++) printf "\r" }')

disk_args=""
if [ -n "${APOLLO_CARTRIDGE_DISK:-}" ]; then
  disk_args="--disk ${APOLLO_CARTRIDGE_DISK}"
fi

# shellcheck disable=SC2086 -- $disk_args is either empty or exactly two words.
exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --cartridge "$cartridge" \
  $disk_args \
  --service-mode \
  --boot-console \
  --boot-input "$knock" \
  --boot-input-interval 400000 \
  --boot-input-after-pc 78E \
  --boot-input-rate 0x88 \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-script "$root/tools/cartridge-boot.script" \
  --clock 2002-11-28 \
  --boot-limit 2500000000 \
  --boot-report \
  "$@"
