#!/bin/sh
# Phase 6's two-node item asks for "console output diffed against itself across
# runs for determinism". This is that check.
#
# Two `--ring-two-node` boots, compared byte for byte. It is a script rather
# than a command line for the two reasons the comparison gets wrong on its own:
#
#   the disks   A run **writes to its Winchesters**. Re-using one pair of files
#               for both runs makes the second run a different experiment -- it
#               starts from a disk the first run modified -- so each run gets
#               its own copies, and the copies are made here rather than
#               remembered.
#
#   the diff    The two runs are given different file names by construction,
#               and those names are printed in the report header. A plain diff
#               therefore reports the harness's own setup as a failure, which is
#               exactly what the first version of this check did
#               (`FINDINGS.md` C194). The `volume` lines are excluded; nothing
#               else is.
#
# The second node is node A's volume with three bytes of its creator UID
# changed, which is what makes it answer a different ring address -- see
# `FINDINGS.md` C180 and the approximation named there.
#
# Usage: tools/ring-determinism.sh [instructions-per-node]   (default 60000000)
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
limit=${1:-60000000}
work=${APOLLO_RING_DET_DIR:-${TMPDIR:-/tmp}/apollo-ring-det.$$}
bin=$root/build/linux-release/src/frontend/headless/apollo-headless
[ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless
base=$root/media/dn3500-sr10.4-installed.awd
prom=$root/roms/firmware/3500_BOOT_12191_7.bin
ring=$root/roms/firmware/3500_RING_10666_6.bin

for f in "$bin" "$base" "$prom" "$ring"; do
  [ -e "$f" ] || { echo "ring-determinism: missing $f" >&2; exit 2; }
done

mkdir -p "$work"
trap 'rm -rf "$work"' EXIT

for i in 1 2; do
  cp "$base" "$work/${i}a.awd"
  cp "$base" "$work/${i}b.awd"
  python3 - "$work/${i}b.awd" <<'PY'
import sys
# The node a machine presents comes from the creator UID at block 0 0x48; the
# field at block 1 0x49 is the one MAME reads. Patch both so the two emulators
# agree about the same disk.
p = sys.argv[1]
with open(p, "r+b") as f:
    f.seek(0x48); uid = bytearray(f.read(8)); uid[5:8] = b"\x02\x22\x22"
    f.seek(0x48); f.write(bytes(uid))
    f.seek(1056); b = bytearray(f.read(0x50)); b[0x49:0x4c] = b"\x02\x22\x22"
    f.seek(1056); f.write(bytes(b))
PY
  "$bin" --boot-prom "$prom" --ring-two-node "$limit" \
    --ring-disk-a "$work/${i}a.awd" --ring-disk-b "$work/${i}b.awd" \
    --ring-rom "$ring" --ring-console --clock 2002-11-28T09:00:00 \
    > "$work/run$i.log" 2>&1
done

if diff -u \
     "$(grep -v '^volume ' "$work/run1.log" > "$work/c1"; echo "$work/c1")" \
     "$(grep -v '^volume ' "$work/run2.log" > "$work/c2"; echo "$work/c2")"
then
  echo "ring determinism OK at $limit instruction(s) per node:"
  echo "  $(grep -c . "$work/c1") line(s) identical, input file names excluded"
  grep -E 'ring +hash|calendar ram|ring slot' "$work/run1.log"
else
  echo "ring determinism FAILED at $limit instruction(s) per node" >&2
  exit 1
fi
