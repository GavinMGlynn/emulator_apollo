#!/bin/sh
# The DS5500's identity baseline, and the reason it exists is a defect that hid
# for three weeks behind the DN3500's.
#
# `tools/identity-boot.sh` boots a **DN3500**, and a DN3500 is a 68030. So a
# change to `src/core/cpu/m68040/` cannot move its hash and cannot be caught by
# it -- which is not hypothetical. The 68040 ATC's `M` write-back was missing;
# `F78D6DBE770CAF47` stayed exactly where it was while a DS5500 restore was
# silently failing to write its root directory, and the defect was found by
# diffing two restores rather than by any harness. The completion plan records
# that as the tail the 68040 item owes, and this is it.
#
# ## The bound is derived, and here it derives itself
#
# The plan is explicit that the bound "is not a free parameter": the
# restore-and-`shut` script consumed **40,000,000,000** steps *exactly*, which
# is a bound being hit rather than a cost being measured, and a baseline built
# on a round number measures the number.
#
# So this run does not carry one. `--boot-stop-on-script-end` stops it when
# `tools/dn5500-md.script`'s last step is satisfied -- the `MD14` banner and the
# prompt behind it -- and the instruction count is then the machine's own
# answer, not a parameter. The `--boot-limit` below is a guard against a machine
# that never gets there, an order of magnitude clear of the real figure.
#
# ## The reference
#
#     executed     8592258 instruction(s)
#     state hash   C3F77989268973A3
#     final PC     00002918 (boot PROM)
#     clocks       27923652
#
# Reproduced across two runs on 2026-09-12. Report the pair the DN3500 harness
# reports: `executed` and `final PC` say the machine ran the same program, and
# the hash alone moving means state was added rather than behaviour changed.
#
# ## What it covers, and what it does not
#
# This is the **boot PROM's** path on a 68040: the I/O protection map, the
# transparent translation registers, the caches, the MMU the PROM programs two
# instructions in, and the board's DS5500 map from `019411-A00` Table 2-5. It is
# 8.6 M instructions and takes about two seconds, so it is cheap enough to run
# on every 68040 change.
#
# **It is not the deep check.** Domain/OS paging, the ATC's write-back and the
# single-level store are exercised by the *restore*, which costs ~40 G steps and
# three quarters of an hour: `tools/dn5500/README.md` has that route, and
# `regression-check-must-postdate-the-change` is why it is worth paying for a
# change that touches them. A fast baseline that catches most things is not an
# argument against the slow one that catches the rest.
#
# Usage: tools/dn5500-identity.sh [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "$root/tools/dn5500-md.sh" \
  --boot-limit 200000000 \
  --boot-stop-on-script-end \
  --boot-report \
  "$@"
