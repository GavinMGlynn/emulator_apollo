#!/bin/sh
# Take a state dump from both machines at the same point and diff them.
#
# The two runs this compares are the *crash* path -- our DN3500 auto-booting
# Domain/OS on the 2026 clock, which reaches `00120020`, and the oracle doing
# the same. It is deliberately not the display-halt path: that one runs code the
# oracle never executes (MAME reports a colour board whatever it is configured
# as, `RING.md`-style evidence in PROJECT_STATUS), so there is nothing to differ
# against there and a differential is the wrong instrument for it.
#
# Both sides need the same instant. `AT` is emulated seconds, which both
# machines measure the same way -- unlike instruction counts, which are not
# comparable across two cores and have produced wrong answers here before.
#
# The oracle half needs `tools/mame-oracle/apollo-state-dump.patch` applied and
# MAME rebuilt; see STATE_DIFF.md.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${OUT:-$root/build/state-compare}
at=${AT:-40}
disk=${DISK:-$root/media/dn3500-sr10.4-installed.awd}
mkdir -p "$out"

# MAME writes to a disk in place, so it gets a copy. Ours reads only, but takes
# one too, so neither run can be blamed for the other's media.
cp -f "$disk" "$out/oracle.awd"
cp -f "$disk" "$out/ours.awd"

echo "# ours: auto-boot, 2026 clock, dumping at the end of a bounded run"
"$root/build/linux-release/src/frontend/headless/apollo-headless" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --disk "$out/ours.awd" --clock 2026-08-09 \
  --boot-input "$(printf '\r')" --boot-input-port 1 --boot-input-channel B \
  --boot-script "$root/tools/boot-domainos.script" \
  --dump-state "$out/ours.txt" "$@" \
  | grep -E "state hash|state dump|final PC|clocks"

echo "# oracle: normal mode, same disk, dumping at ${at}s emulated"
APOLLO_STATE_DUMP="$out/theirs.txt" APOLLO_STATE_DUMP_AT="$at" \
APOLLO_MD_POST=none \
  python3 "$root/tools/mame-oracle/mdsession.py" --stage watch \
  --disk "$out/oracle.awd" --settle 900 \
  | grep -E "Graphics Controller|Normal/Service|state dump" || true

echo "# diff"
python3 "$root/tools/state-diff.py" "$out/ours.txt" "$out/theirs.txt" \
  --map "$root/tools/mame-oracle/state-map.txt" || true
