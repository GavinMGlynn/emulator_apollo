#!/bin/sh
# Interleaved A/B timing of two frontend binaries over the reference boot.
#
# ## Why this exists rather than `time` twice
#
# This machine's wall clock **drifts**. The same binary measured 29.6 s and
# 32.5 s on runs taken minutes apart -- a 2.9 s spread, wider than any
# optimisation in Phase 8 has been worth. Timing A, then changing the code, then
# timing B compares two different machines as much as two different builds.
#
# That is not hypothetical. Two changes were mis-measured this way in one
# session: a timer gate was called "neutral", reverted, and given a confident
# comment explaining why it could not pay -- interleaving showed it faster in
# all three pairs. And a serial gate was recorded as a 1.9% win on a single run
# and turned out to have no direction at all.
#
# So: build both binaries **first**, then alternate them inside one window, so
# whatever the machine is doing happens to both. Report the minimum as well as
# the mean, because noise only ever adds -- the minimum is the closest estimate
# of the true cost, and the mean is what a busy machine did to it.
#
# ## What it does not do
#
# It does not decide anything. Three pairs is enough to see a 5% effect and not
# enough to see a 1% one; when the pairs disagree and the minima and means point
# different ways, **that is the answer** -- the effect is below the floor -- and
# the honest conclusion is "no measurable difference", not "the mean says A".
#
# It does not check correctness either. A faster binary that changed the state
# hash is a broken binary, so run `tools/identity-boot.sh` on the candidate and
# compare the hash and the whole report; this measures only how long it took.
#
# Usage:  tools/ab-boot.sh A_BINARY B_BINARY [PAIRS]
set -eu

if [ $# -lt 2 ]; then
  echo "usage: $0 A_BINARY B_BINARY [PAIRS]" >&2
  echo "  build both binaries before running: rebuilding between timings is" >&2
  echo "  exactly the drift this script exists to defeat." >&2
  exit 2
fi

a=$1
b=$2
pairs=${3:-3}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

for bin in "$a" "$b"; do
  [ -x "$bin" ] || { echo "$0: $bin is not executable" >&2; exit 2; }
done

prom="$root/roms/firmware/3500_BOOT_12191_7.bin"
disk="$root/media/dn3500-sr10.4-installed.awd"
for f in "$prom" "$disk"; do
  [ -r "$f" ] || { echo "$0: cannot read $f (roms/ and media/ are gitignored)" >&2; exit 2; }
done

# The reference boot's own invocation, kept in step with identity-boot.sh --
# the same dialogue and limit, because a timing over a different workload is a
# timing of something else.
run() {
  start=$(date +%s.%N)
  "$1" \
    --boot-prom "$prom" \
    --disk "$disk" \
    --boot-console \
    --boot-input "$(printf '\r')" \
    --boot-input-port 1 \
    --boot-input-channel B \
    --boot-script "$root/tools/boot-domainos.script" \
    --boot-limit 350000000 >/dev/null 2>&1
  end=$(date +%s.%N)
  awk -v s="$start" -v e="$end" 'BEGIN { printf "%.3f", e - s }'
}

printf 'A = %s\nB = %s\n%d pair(s), alternating\n\n' "$a" "$b" "$pairs"

atimes=
btimes=
i=1
while [ "$i" -le "$pairs" ]; do
  ta=$(run "$a")
  tb=$(run "$b")
  printf '  pair %d   A %ss   B %ss   %s\n' "$i" "$ta" "$tb" \
    "$(awk -v x="$ta" -v y="$tb" 'BEGIN { print (x < y) ? "A" : (y < x) ? "B" : "=" }')"
  atimes="$atimes $ta"
  btimes="$btimes $tb"
  i=$((i + 1))
done

summarise() {
  awk -v label="$1" 'BEGIN { min = 1e9; sum = 0; n = 0 }
    { for (i = 1; i <= NF; i++) { v = $i + 0; sum += v; n++; if (v < min) min = v } }
    END { printf "  %-8s min %.3fs   mean %.3fs   over %d run(s)\n", label, min, sum / n, n }'
}
echo
printf '%s\n' "$atimes" | summarise A
printf '%s\n' "$btimes" | summarise B

# The verdict, and it is deliberately conservative: a split decision is a null
# result rather than a tie broken by the mean.
awk -v a="$atimes" -v b="$btimes" 'BEGIN {
  na = split(a, xa, " "); nb = split(b, xb, " ")
  wins = 0; pairs = 0
  for (i = 1; i <= na && i <= nb; i++) {
    if (xa[i] == "" || xb[i] == "") continue
    pairs++
    if (xa[i] + 0 < xb[i] + 0) wins++
  }
  print ""
  if (wins == pairs)      printf "  A faster in all %d pairs\n", pairs
  else if (wins == 0)     printf "  B faster in all %d pairs\n", pairs
  else                    printf "  split %d/%d -- no direction at this sample size; read as no measurable difference\n", wins, pairs
}'
