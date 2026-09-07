#!/bin/sh
# The boot that reaches a **shell** -- the Mnemonic Debugger on the serial
# console, `DI W` / `EX DOMAIN_OS` to a `)` prompt, `SH`, `login: user`, and
# `/com/lcnode` -- recorded so it does not have to be found again.
#
# This is `FINDINGS.md` C164's route, which was proven on the oracle in August
# and which C165 then declared impossible on this core. C165 was wrong about the
# reason and right about the symptom, and three things had to be found before it
# worked. All three are in the invocation below, and none of them is tuning:
#
#   --boot-input-after-pc 78E   `--boot-input-interval` sets the **spacing**
#                        between characters and not the **offset** of the
#                        first, so every earlier attempt delivered its burst at
#                        t=0 and had it discarded. `00078E` is the service-mode
#                        console-election poll; this holds the typing until the
#                        machine is sitting in it. Measured: `0 of 60` becomes
#                        `120 of 120`.
#   --boot-input-rate 0x88   **2400 baud, and the firmware chose it, not us.**
#                        The election sets `ACR = $E0` and `CSRB = $77` --
#                        baud set 2, code 7, which the SCN2681 makes **2000
#                        baud** -- and its autobaud table at `$822` maps five
#                        shapes: `$FF`->9600, `$FE`->4800, `$C7`->2400,
#                        `$72`->1200, `$C0`->300. This core's resampler
#                        reproduces all five exactly (`FINDINGS.md` C240), and
#                        only the three slow ones are **self-consistent** at a
#                        2000-baud receiver: a 2400 terminal's `0D` reads as
#                        `$C7` and is answered with 2400. A 9600 terminal's
#                        reads as `$FE`, is answered with 4800, and then
#                        `0008C8`'s `cmp.b #$d, d1` / `bne.w $752` throws the
#                        whole election away -- sixty-three times in one run.
#   --service-mode       the switch the election needs. In Normal mode the PROM
#                        never reaches `00078E` at all, with or without
#                        `--configure`, so paced returns cannot interrupt a
#                        console-only boot into MD.
#
# Where it gets to, measured 2026-09-08 on `dn3500-sr10.4-installed.awd`:
# `MD7C REV 8.00, 1989/08/16.17:23:52`, a `>` prompt per carriage return,
# `Domain/OS kernel(7)`, `Apollo Phase II Environment`, `login:`,
# `Registries unavailable. You are logged in as user.none.none.`, `$`, and
#
#     The node ID of this node is 12345.
#     No other nodes responded.
#     12345   2002/11/28 12:01:56   2002/11/28 12:03:06  //node_12345
#
# byte for byte as C164 recorded it on the oracle. About 1.1 G instructions to
# the `$` prompt; 2.5 G leaves room for the command and its answer.
#
# **Run on a copy.** The core writes to the image it is given.
#
# Usage: tools/md-shell.sh <disk-image-copy> [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

if [ $# -lt 1 ]; then
  echo "usage: tools/md-shell.sh <disk-image-copy> [args...]" >&2
  exit 2
fi
disk=$1
shift

# 120 carriage returns at 0.4 s: the pace `docs/references/MD.md` records from
# the other side, and enough of them to outlast the election's own retries.
knock=$(awk 'BEGIN { for (i = 0; i < 120; i++) printf "\r" }')

exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --disk "$disk" \
  --service-mode \
  --boot-console \
  --boot-input "$knock" \
  --boot-input-interval 400000 \
  --boot-input-after-pc 78E \
  --boot-input-rate 0x88 \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-script "$root/tools/md-shell.script" \
  --clock 2002-11-28 \
  --boot-limit 2500000000 \
  --boot-report \
  "$@"
