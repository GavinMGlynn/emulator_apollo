#!/bin/sh
# MD on a DS5500: the DN5500 boot PROM in service mode, with a cartridge and a
# disk fitted, driven to its `>` prompt.
#
# **It exists because the invocation is not guessable from the DN3500's.** Three
# parts differ and none of them leaves a trace in the report:
#
#   --model dn5500   selects `019411-A00` Table 2-5's map, which is what makes
#                    `07000000` answer. Without the I/O protection map the PROM
#                    bus-errors clearing it at `000698` before it has a stack
#                    and the machine is dead at 137 instructions -- the failure
#                    this script's existence is the fix for.
#   the PROM         `5500_BOOT_A1631-80046_1-30-92.bin`, and its banner is
#                    `MD14` where a DN3500's is `MD7C`.
#   no --boot-input-after-pc
#                    `tools/cartridge-boot.sh` fires the knock at PC `78E`,
#                    which is a *DN3500 PROM* address. This PROM reaches its
#                    console election on its own timeline, and the knock paced
#                    at 400 us with `--boot-input-rate 0x88` is enough.
#
# The 2400 baud (`0x88`) and the 120-carriage-return knock are the DN3500's and
# are unchanged: the firmware chooses the rate, not us (`md-shell-on-the-serial-
# console`).
#
# Usage: tools/dn5500-md.sh [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

# A copy, always: MAME and this core both write to a mounted cartridge, and a
# run that mutates the boot cartridge is the trap `an-input-a-run-can-write-is-
# not-an-input` records.
cartridge=${APOLLO_CARTRIDGE:-}
if [ -z "$cartridge" ]; then
  cartridge=${APOLLO_SCRATCH:-/home/gavin/apollo-scratch}/dn5500-boot-cartridge.ct
  cp "$root/media/domainos/019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct" "$cartridge"
fi

knock=$(awk 'BEGIN { for (i = 0; i < 120; i++) printf "\r" }')

disk_args=""
if [ -n "${APOLLO_DISK:-}" ]; then
  disk_args="--disk ${APOLLO_DISK}"
fi

exec "$bin" \
  --model dn5500 \
  --boot-prom "$root/roms/firmware/5500_BOOT_A1631-80046_1-30-92.bin" \
  --cartridge "$cartridge" \
  $disk_args \
  --service-mode \
  --boot-console \
  --boot-input "$knock" \
  --boot-input-interval 400000 \
  --boot-input-rate 0x88 \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-script "$root/tools/dn5500-md.script" \
  --clock 2002-11-28 \
  --boot-limit 900000000 \
  --boot-report \
  "$@"
