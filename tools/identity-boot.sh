#!/bin/sh
# The identity harness's reference boot: 350 M instructions of Domain/OS SR10.4
# on a DN3500, printing the state hash every optimisation is judged against.
#
# It exists as a script rather than as a command line in a document because a
# state hash is only a reference *together with the invocation that produced
# it*, and several of that invocation's parts leave no trace in the report:
#
#   --screen      a fitted display puts its image memory into the hash. Omitting
#                 it changes the number and **nothing else** -- every counter,
#                 register and console line is byte-identical -- so the run
#                 reads as a broken change rather than a different machine.
#   --clock       the calendar's registers are hashed, and the epoch is not
#                 printed anywhere else.
#   the dialogue  --boot-input $'\r' on port 1 channel B is not conversation:
#                 the PROM autobauds and cannot transmit until it has received a
#                 character. The `y` must come from --boot-script, which waits
#                 for the prompt; passing it with --boot-input sends it early,
#                 it is swallowed, and the machine sits in the PROM to the
#                 instruction limit while still reporting the character as
#                 delivered.
#
# Both invisible inputs are now printed by the run itself -- `power-on` in the
# header and `fitted` beside the hash -- so a report can be checked against this
# script rather than trusted.
#
# Reference configuration: no display fitted, default 1987 epoch, 16 Mbyte.
#
# **This clock is not the one the crash investigation wants.** The volume was
# installed in 2026 and its timestamps are in that era, so a 1987 calendar is
# thirty-nine years behind it: the machine takes a different path, produces
# `392 x vector 2` against `939`, and by 350 M instructions has not reached the
# `00120020` crash at all. Pass `--clock 2026-08-09` for any question about the
# crash, and leave the default alone for identity -- the hash is a promise about
# determinism and changing what it covers would break it silently.
# Reference hash: see docs/PROJECT_STATUS.md. Release builds only for timing;
# the hash is identical on every build type and that is asserted by using it.
#
# Usage: tools/identity-boot.sh [extra apollo-headless arguments...]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$root/build/linux-release/src/frontend/headless/apollo-headless
[ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless

exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --disk "$root/media/dn3500-sr10.4-installed.awd" \
  --boot-console \
  --boot-input "$(printf '\r')" \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-script "$root/tools/boot-domainos.script" \
  --boot-limit 350000000 \
  "$@"
