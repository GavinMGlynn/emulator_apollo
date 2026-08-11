#!/bin/sh
# Take a state dump from both machines at the same point in the same program,
# and diff them.
#
# ## Why the point is a program counter and not an instant
#
# The first version of this script synchronised on **emulated seconds**, on the
# reasoning that both machines measure time the same way while instruction
# counts are not comparable across two cores. The second half of that is true
# and the conclusion does not follow: two cores whose timing differs at all are
# at *different points in the program* at the same instant, so a diff taken
# there reports the difference between two unrelated machine states. It is the
# same error as comparing snapshots of two machines with no shared clock, which
# has already cost this project five withdrawn conclusions.
#
# A program counter, with a visit count, is sample-free: "the Nth time this
# instruction is about to run" means the same thing on both machines however
# fast either got there. It is also the only kind of point at which a
# difference can be *attributed*, because both machines have executed the same
# instructions to reach it.
#
# ## The off-by-one, which is real and is handled on the oracle's side
#
# MAME's breakpoint stops **before** the instruction at the address; this
# core's `--boot-stop-pc` stops **after** it. `statesync.lua` therefore steps
# the oracle once after the hit, so both dumps are of a machine that has just
# executed the instruction at PC. Without that, every register the instruction
# touched shows as a difference and reads exactly like an emulator bug.
#
# ## Requirements
#
# The oracle needs `tools/mame-oracle/apollo-state-dump.patch` applied and MAME
# rebuilt (see STATE_DIFF.md), and it is run with `-debug -debugger none`, which
# is what makes `machine.debugger` exist without wanting a window.
#
# Usage: PC=653A [SKIP=0] [MODE=normal] tools/state-compare.sh [extra ours args]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${OUT:-$root/build/state-compare}
pc=${PC:?set PC to the hex address to stop at, e.g. PC=653A}
skip=${SKIP:-0}
mode=${MODE:-normal}
disk=${DISK:-$root/media/dn3500-sr10.4-installed.awd}
prom=${PROM:-$root/roms/firmware/3500_BOOT_12191_7.bin}
mame=${MAME:-$root/ext/mame/apollo}
mkdir -p "$out"

# MAME writes to a disk in place, so it gets a copy. Ours reads only, but takes
# one too, so neither run can be blamed for the other's media.
cp -f "$disk" "$out/oracle.awd"
cp -f "$disk" "$out/ours.awd"

bin=$root/build/linux-release/src/frontend/headless/apollo-headless
[ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless

echo "# ours: stop at PC $pc, skipping $skip"
"$bin" \
  --boot-prom "$prom" --disk "$out/ours.awd" \
  --boot-stop-pc "$pc" --boot-stop-pc-skip "$skip" \
  --boot-limit "${LIMIT:-350000000}" \
  --dump-state "$out/ours.txt" "$@" \
  | grep -E "stopped at|state hash|state dump"

echo "# oracle: same address, same count, ${mode} mode"
rm -rf "$out/rt"; mkdir -p "$out/rt/nvram" "$out/rt/cfg" "$out/rt/state" \
  "$out/rt/diff" "$out/rt/snapshot" "$out/rt/input"
APOLLO_SYNC_PC="$pc" APOLLO_SYNC_SKIP="$skip" APOLLO_SYNC_MODE="$mode" \
APOLLO_SYNC_DUMP="$out/theirs.txt" APOLLO_SYNC_GIVEUP="${GIVEUP:-120}" \
  "$mame" dn3500 -noreadconfig \
  -rompath "$root/tools/mame-oracle/out/roms" \
  -video none -sound none -nothrottle \
  -debug -debugger none \
  -autoboot_script "$root/tools/mame-oracle/statesync.lua" \
  -disk1 "$out/oracle.awd" \
  -nvram_directory "$out/rt/nvram" -cfg_directory "$out/rt/cfg" \
  -state_directory "$out/rt/state" -diff_directory "$out/rt/diff" \
  -snapshot_directory "$out/rt/snapshot" -input_directory "$out/rt/input" \
  2>&1 | grep -E "^#"

echo "# diff"
python3 "$root/tools/state-diff.py" "$out/ours.txt" "$out/theirs.txt" \
  --map "$root/tools/mame-oracle/state-map.txt" || true
