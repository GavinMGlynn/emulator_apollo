#!/bin/sh
# Boot a DS5500 from its own disk, the way a machine in NORMAL mode does.
#
# **The difference from `tools/dn5500-md.sh` is one flag and it matters.** That
# script passes `--service-mode`, which `008860-A03` p. 1-12 step 1 says *not* to
# use for an install ("Make sure the node is in NORMAL (versus SERVICE) mode"),
# and which turns out to be unnecessary: a node with no bootable volume falls
# back to MD on its own, which is what the run of 2026-09-11 showed when it
# reached `MD14 REV 2.00` with no service mode at all. Use `dn5500-md.sh` to
# *reach MD*; use this to find out what the volume does.
#
# What it prints is the discriminator, and every line of it means something:
#
#   error: sysboot not found              the boot area is empty or mis-shaped
#   error: incorrect sysboot installed    the records are there, the tag is wrong
#   boot error: SAU14 not found in root_dir   the SYSBOOT ran and mounted the
#                                         volume; what is missing is a directory
#   Loaded: SELF_TEST Revision: ...       the boot path is whole
#   Self tests passed.                    the machine's own diagnostic is clean
#   Domain/OS kernel(14), revision 10.4   and the kernel is running
#
# **`--configure` is not optional here.** Without it the calendar's battery RAM
# carries no VALID PATTERN at `010912` -- `002398-04` p. 12-3's field -- and the
# diagnostic's `CPU (bus error) Test #0` stops with
# `Expected= 00000000, Actual= 00000012, Address= 00010912`, having already told
# you why: "Configuration information is not initialized ... type \"ex config\"".
# Sealing the table is what `ex config` would do on the real machine, and with it
# every sub-test passes and the boot goes on to load Domain/OS.
#
# **The cartridge is still fitted**, and deliberately: the firmware's load-path
# test walks the devices it has, and a DS5500 with no cartridge is a different
# machine from the one this project has been measuring. A copy, always -- see
# `dn5500-md.sh` for why.
#
# Usage: APOLLO_DISK=<image> tools/dn5500-boot.sh [extra arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=${APOLLO_HEADLESS_BIN:-}
if [ -z "$bin" ]; then
  bin=$root/build/linux-release/src/frontend/headless/apollo-headless
  [ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
fi

if [ -z "${APOLLO_DISK:-}" ]; then
  echo "dn5500-boot.sh: set APOLLO_DISK to the volume to boot" >&2
  exit 2
fi

cartridge=${APOLLO_CARTRIDGE:-}
if [ -z "$cartridge" ]; then
  cartridge=${APOLLO_SCRATCH:-/home/gavin/apollo-scratch}/dn5500-boot-cartridge.ct
  cp "$root/media/domainos/019593-001.CRTG_STD_SFW_BOOT_1-REV.A.ct" "$cartridge"
fi

# The same 120-carriage-return knock at 2400 baud as `dn5500-md.sh`: the console
# election is the firmware's and it chooses the rate, not us.
knock=$(awk 'BEGIN { for (i = 0; i < 120; i++) printf "\r" }')

exec "$bin" \
  --model dn5500 \
  --configure \
  --boot-prom "$root/roms/firmware/5500_BOOT_A1631-80046_1-30-92.bin" \
  --cartridge "$cartridge" \
  --disk "$APOLLO_DISK" \
  --boot-console \
  --boot-input "$knock" \
  --boot-input-interval 400000 \
  --boot-input-rate 0x88 \
  --boot-input-port 1 \
  --boot-input-channel B \
  --clock 2002-11-28 \
  --boot-limit 8000000000 \
  --boot-report \
  "$@"
